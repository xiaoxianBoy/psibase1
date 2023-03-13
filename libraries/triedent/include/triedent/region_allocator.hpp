#pragma once

#include <triedent/gc_queue.hpp>
#include <triedent/object_db.hpp>
#include <triedent/ring_allocator.hpp>

#include <bitset>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <optional>

namespace triedent
{
   // This allocator splits the data into fixed size regions
   // - All allocation is done from the current region
   // - There may be an empty region designated as the next region
   // When the current region becomes full
   // - If there is no next region, extend the file
   // - Otherwise set current region = next region
   // - If any region is less than half full, evacuate the least full region
   //   and make it the next region
   class region_allocator
   {
     public:
      region_allocator(gc_queue&                    gc,
                       object_db&                   obj_ids,
                       const std::filesystem::path& path,
                       access_mode                  mode,
                       std::uint64_t                initial_size = 64 * 1024 * 1024);
      ~region_allocator();
      // MUST NOT hold a session lock
      void* allocate(object_id id, std::uint32_t size, auto&& init)
      {
         std::uint64_t   used_size = alloc_size(size);
         std::lock_guard l{_mutex};
         auto            result = allocate_impl(id, size, used_size);
         init(result, object_location{.offset = _h->alloc_pos, .cache = _level});
         _h->alloc_pos += used_size;
         return result;
      }
      void           deallocate(object_location loc);
      object_header* get_object(std::uint64_t offset)
      {
         return reinterpret_cast<object_header*>(_base + offset);
      }
      object_header*        get_object(object_location loc) { return get_object(loc.offset); }
      std::span<const char> span() const
      {
         return {reinterpret_cast<const char*>(_file.data()), _file.size()};
      }

     private:
      static constexpr std::uint64_t alloc_size(std::size_t size)
      {
         return ((size + 7) & -8) + sizeof(object_header);
      }
      void* allocate_impl(object_id id, std::uint32_t size, std::uint32_t used_size);
      void  deallocate(std::uint64_t region, std::uint32_t used_size);
      static constexpr std::uint64_t max_regions   = 64;
      static constexpr std::uint64_t max_queue     = 32;
      static constexpr std::uint64_t page_size     = 4096;
      static constexpr std::uint64_t pending_write = 1ull << 48;
      struct queue_item
      {
         std::atomic<std::uint64_t> dest_begin;
         std::atomic<std::uint64_t> dest_end;
         std::atomic<std::uint64_t> src_begin;
         std::atomic<std::uint64_t> src_end;
      };
      struct header
      {
         struct data
         {
            // TODO: alloc_pos should be atomic.
            // TODO: num_regions/current_region can be smaller
            std::uint64_t              region_size;
            std::uint64_t              alloc_pos;
            std::uint64_t              num_regions;
            std::uint64_t              current_region;
            std::atomic<std::uint64_t> region_used[max_regions];
         };
         // current switches between the two elements of the array,
         // so that complex mutations never leave behind an invalid
         // intermediate state on crash.
         data                       regions[2];
         std::atomic<std::uint32_t> current;
         queue_item                 queue[max_queue];
         char*                      base() { return reinterpret_cast<char*>(this) + page_size; }
      };
      static_assert(sizeof(header) < page_size);

      static std::pair<std::uint64_t, std::uint64_t> get_smallest_region(header::data* h);
      std::optional<std::uint64_t>                   get_free_region(std::size_t num_regions);
      void          start_new_region(header::data* old, header::data* next);
      void          double_region_size(header::data* old_data, header::data* new_data);
      static void   copy_header_data(header::data* old, header::data* next);
      std::uint64_t evacuate_region(queue_item& item);

      static bool is_used(const queue_item& item);
      //
      void load_queue();
      bool push_queue(std::uint64_t region, std::uint64_t used);
      bool run_one();
      void run();

      void make_available(std::uint64_t region);
      void reevaluate_free();

      std::mutex    _mutex;
      gc_queue&     _gc;
      object_db&    _obj_ids;
      mapping       _file;
      header*       _header;
      header::data* _h;
      char*         _base;

      std::bitset<max_regions>      _free_regions;
      std::uint64_t                 _queue_pos;
      std::uint64_t                 _queue_front;
      std::condition_variable       _pop_cond;
      bool                          _done;
      std::thread                   _thread;
      static constexpr std::uint8_t _level = 3;
   };
}  // namespace triedent
