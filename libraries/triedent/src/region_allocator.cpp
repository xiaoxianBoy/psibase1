#include <triedent/region_allocator.hpp>

namespace triedent
{

   region_allocator::region_allocator(gc_queue&                    gc,
                                      object_db&                   obj_ids,
                                      const std::filesystem::path& path,
                                      access_mode                  mode,
                                      std::uint64_t                initial_size)
       : _gc(gc), _obj_ids(obj_ids), _file(path, mode), _done(false)
   {
      if (_file.size() == 0)
      {
         _file.resize(page_size + initial_size);
         new (_file.data()) header{.regions = {{.region_size    = initial_size,
                                                .alloc_pos      = 0,
                                                .num_regions    = 1,
                                                .current_region = 0,
                                                .region_used    = {initial_size}},
                                               {}},
                                   .current = 0};
      }

      _header = reinterpret_cast<header*>(_file.data());
      _h      = &_header->regions[_header->current.load()];
      _base   = _header->base();
      load_queue();
      _thread = std::thread{[this] { run(); }};
   }

   region_allocator::~region_allocator()
   {
      _done = true;
      _thread.join();
   }

   void* region_allocator::allocate_impl(object_id id, std::uint32_t size, std::uint32_t used_size)
   {
      auto alloc_pos = _h->alloc_pos;
      auto available = (_h->current_region + 1) * _h->region_size - _h->alloc_pos;
      if (used_size > available)
      {
         // create a dummy object in the remaining space
         if (available)
         {
            new (_base + alloc_pos)
                object_header{.size = available - sizeof(object_header), .id = 0};
         }
         deallocate(_h->current_region, available + pending_write);
         // switch to the next region
         auto next_index = _header->current.load() ^ 1;
         start_new_region(_h, &_header->regions[next_index]);
         _h        = &_header->regions[next_index];
         alloc_pos = _h->alloc_pos;
         _header->current.store(next_index);

         if (_header->regions[0].region_size != _header->regions[1].region_size)
            reevaluate_free();

         // Try to free some space
         auto [smallest, small_size] = get_smallest_region(_h);
         if (small_size < _h->region_size / 2)
         {
            push_queue(smallest, small_size);
         }
      }
      void* result = _base + _h->alloc_pos;
      auto  o      = new (result) object_header{.size = size, .id = id.id};
      return o->data();
   }
   void region_allocator::deallocate(object_location loc)
   {
      std::lock_guard l{_mutex};
      assert(loc.cache == _level);
      auto region      = loc.offset / _h->region_size;
      auto object_used = sizeof(object_header) + get_object(loc)->data_capacity();
      deallocate(region, object_used);
   }
   void region_allocator::deallocate(std::uint64_t region, std::uint32_t used_size)
   {
      auto total_used = _h->region_used[region].load();
      assert(used_size <= total_used);
      _h->region_used[region].store(total_used - used_size);
      if (total_used == used_size)
      {
         make_available(region);
      }
   }
   std::pair<std::uint64_t, std::uint64_t> region_allocator::get_smallest_region(header::data* h)
   {
      std::uint64_t min     = h->region_size;
      std::uint64_t min_pos = 0;
      for (std::size_t i = 0; i < h->num_regions; ++i)
      {
         if (h->region_used[i] != 0)
         {
            if (h->region_used[i] < min)
            {
               min     = h->region_used[i];
               min_pos = i;
            }
         }
      }
      return {min_pos, min};
   }
   std::optional<std::uint64_t> region_allocator::get_free_region(std::size_t num_regions)
   {
      for (std::size_t i = 0; i < num_regions; ++i)
      {
         if (_free_regions.test(i))
            return i;
      }
      return std::nullopt;
   }
   void region_allocator::start_new_region(header::data* old, header::data* next)
   {
      auto num_regions = old->num_regions;
      if (auto next_region = get_free_region(num_regions))
      {
         copy_header_data(old, next);
         next->current_region = *next_region;
      }
      else
      {
         if (num_regions == max_regions)
         {
            double_region_size(old, next);
         }
         else
         {
            copy_header_data(old, next);
         }
         _gc.push(_file.resize(_file.size() + next->region_size));
         _header = reinterpret_cast<header*>(_file.data());
         _base   = _header->base();
         next->region_used[next->num_regions].store(next->region_size);
         next->current_region = next->num_regions;
         next->num_regions++;
      }
      next->region_used[next->current_region].store(next->region_size + pending_write);
      _free_regions.reset(next->current_region);
      next->alloc_pos = next->current_region * next->region_size;
   }
   void region_allocator::double_region_size(header::data* old_data, header::data* new_data)
   {
      auto num_regions = old_data->num_regions;
      assert(num_regions % 2 == 0);
      new_data->region_size = old_data->region_size * 2;
      new_data->num_regions = num_regions / 2;
      for (std::size_t i = 0; i < num_regions / 2; ++i)
      {
         _free_regions[i] = _free_regions[2 * i] & _free_regions[2 * i + 1];
         auto new_used =
             old_data->region_used[2 * i].load() + old_data->region_used[2 * i + 1].load();
         new_data->region_used[i].store(new_used);
      }
   }
   void region_allocator::copy_header_data(header::data* old, header::data* next)
   {
      next->region_size = old->region_size;
      next->num_regions = old->num_regions;
      for (std::uint32_t i = 0; i < old->num_regions; ++i)
      {
         next->region_used[i].store(old->region_used[i].load());
      }
   }

   std::uint64_t region_allocator::evacuate_region(queue_item& item)
   {
      auto begin    = item.src_begin.load();
      auto end      = item.src_end.load();
      auto dest     = item.dest_begin.load();
      auto dest_end = item.dest_end.load();
      while (begin != end)
      {
         object_header*  p = get_object(begin);
         object_location loc{.offset = begin, .cache = _level};
         object_id       id{p->id};
         auto            info = _obj_ids.get(id);
         if (info.ref && info == loc)
         {
            auto lock = _obj_ids.lock(id);
            info      = _obj_ids.get(id);
            if (info.ref && info == loc)
            {
               auto object_size = sizeof(object_header) + p->data_capacity();
               if (object_size > dest_end - dest)
               {
                  break;
               }
               void* new_location = _base + dest;
               std::memcpy(new_location, p, object_size);
               item.dest_begin.store(dest + object_size);
               _obj_ids.move(lock, object_location{.offset = dest, .cache = _level});
               dest += object_size;
            }
         }
         begin += (sizeof(object_header) + p->data_capacity());
         item.src_begin.store(begin);
      }
      return dest;
   }

   bool region_allocator::is_used(const queue_item& item)
   {
      return item.dest_end.load() > item.dest_begin.load();
   }

   void region_allocator::load_queue()
   {
      _queue_front = _queue_pos = 0;
      // Find the first free item if there is one
      for (std::uint64_t i = 0; i < max_queue; ++i)
      {
         if (!is_used(_header->queue[i]))
         {
            _queue_pos   = i;
            _queue_front = (i + 1) % max_queue;
         }
      }
      // Remove existing pending writes
      for (auto& used : _h->region_used)
      {
         auto val = used.load();
         val %= pending_write;
         used.store(val);
      }
      // fix pending writes
      for (const auto& item : _header->queue)
      {
         if (is_used(item))
         {
            auto& used = _h->region_used[item.dest_begin.load() / _h->region_size];
            auto  val  = used.load();
            val += pending_write;
            used.store(val);
         }
      }
      // mark current_region as pending again
      {
         auto& used = _h->region_used[_h->current_region / _h->region_size];
         auto  val  = used.load();
         val += pending_write;
         used.store(val);
      }
      // find free regions
      for (std::uint64_t i = 0; i < _h->num_regions; ++i)
      {
         _free_regions.set(i, _h->region_used[i].load() == 0);
      }
   }

   bool region_allocator::push_queue(std::uint64_t region, std::uint64_t used)
   {
      std::uint64_t pos = _queue_pos;
      if (is_used(_header->queue[_queue_pos]))
      {
         return false;
      }
      auto& item = _header->queue[_queue_pos];
      // Ensure that the item will not be considered used until
      // the final store to dest_end.
      item.dest_end.store(0);
      item.src_begin.store(region * _h->region_size);
      item.src_end.store((region + 1) * _h->region_size);
      auto alloc_pos = _h->alloc_pos;
      item.dest_begin.store(alloc_pos);
      alloc_pos += used;
      _h->alloc_pos = alloc_pos;
      _h->region_used[region].store(_h->region_used[region].load() + pending_write);
      item.dest_end.store(alloc_pos);
      _queue_pos = (_queue_pos + 1) % max_queue;
      _pop_cond.notify_one();
      return true;
   }

   bool region_allocator::run_one()
   {
      std::unique_lock l{_mutex};
      _pop_cond.wait(
          l, [this]
          { return _done || _queue_front != _queue_pos || is_used(_header->queue[_queue_front]); });
      if (!_done)
      {
         return false;
      }
      auto& item   = _header->queue[_queue_front];
      _queue_front = (_queue_front + 1) % max_queue;
      l.unlock();
      if (is_used(item))
      {
         auto orig_src  = item.src_begin.load();
         auto orig_dest = item.dest_begin.load();
         auto end       = evacuate_region(item);
         l.lock();
         auto src_region  = orig_src / _h->region_size;
         auto dest_region = orig_dest / _h->region_size;
         auto used        = _h->region_used[dest_region].load();
         auto dest_end    = item.dest_end.load();
         auto extra       = dest_end - end;
         auto copied      = end - orig_dest;
         // fill any excess space at the end of the dest buffer with empty objects
         if (extra)
         {
            static constexpr std::uint32_t max_fill = (1u << 24);
            while (end > max_fill + item.dest_end.load())
            {
               new (_base + end) object_header{.size = max_fill - sizeof(object_header), .id = 0};
               end += max_fill;
               item.dest_begin.store(end);
            }
            new (_base + end)
                object_header{.size = dest_end - end - sizeof(object_header), .id = 0};
            item.dest_begin.store(dest_end);
         }
         // Decrement the size of the source buffer and queue it for re-use
         {
            auto src_used = _h->region_used[src_region].load();
            assert(copied <= src_used);
            if (src_used != 0)
            {
               // after a crash, region_used might not get decremented.
               // If we fully evacuate a region, reset the amount to 0.
               if (item.src_begin.load() - orig_src == _h->region_size)
               {
                  src_used = 0;
               }
               else
               {
                  src_used -= copied;
               }
               _h->region_used[src_region].store(src_used);
               if (src_used == 0)
               {
                  make_available(src_region);
               }
            }
         }
         _h->region_used[dest_region].store(used - pending_write - extra);
         if (used == pending_write + extra)
         {
            make_available(dest_region);
         }
      }
      return true;
   }

   void region_allocator::make_available(std::uint64_t region)
   {
      struct make_region_available
      {
         ~make_region_available()
         {
            std::lock_guard l{_self->_mutex};
            if (_self->_h->region_size == region_size)
            {
               // duplicate free would be disasterous
               assert(!_self->_free_regions.test(region));
               _self->_free_regions.set(region);
            }
         }
         region_allocator* _self;
         std::uint64_t     region;
         std::uint64_t     region_size;
      };
      _gc.push(std::make_shared<make_region_available>(this, region, _h->region_size));
   }

   void region_allocator::reevaluate_free()
   {
      for (std::size_t i = 0, end = _h->num_regions; i != end; ++i)
      {
         if (_h->region_used[i] == 0 && !_free_regions.test(i))
         {
            make_available(i);
         }
      }
   }

   void region_allocator::run()
   {
      while (run_one())
      {
      }
   }

}  // namespace triedent
