#pragma once
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

#include <boost/interprocess/file_mapping.hpp>
#include <boost/interprocess/mapped_region.hpp>
#include <triedent/debug.hpp>

namespace triedent
{
   namespace bip = boost::interprocess;

   struct object_id
   {
      uint64_t    id : 40 = 0;  // obj id
      explicit    operator bool() const { return id != 0; }
      friend bool operator==(object_id a, object_id b) { return a.id == b.id; }
      friend bool operator!=(object_id a, object_id b) { return a.id != b.id; }
   } __attribute__((packed)) __attribute((aligned(1)));
   static_assert(sizeof(object_id) == 5, "unexpected padding");

   struct object_header
   {
      // size might not be a multiple of 8, next object is at data() + (size+7)&-8
      uint64_t size : 24;  // bytes of data, not including header
      uint64_t id : 40;

      inline bool     is_free_area() const { return size == 0; }
      inline uint64_t free_area_size() const { return id; }
      inline uint64_t data_size() const { return size; }
      inline uint32_t data_capacity() const { return (size + 7) & -8; }
      inline char*    data() const { return (char*)(this + 1); }
      inline void     set_free_area_size(uint64_t s) { size = s, id = 0; }
      inline void     set(object_id i, uint32_t numb) { size = numb, id = i.id; }
   };
   static_assert(sizeof(object_header) == 8, "unexpected padding");

   // Not stored
   struct object_location
   {
      enum object_type
      {
         leaf  = 0,
         inner = 1
      };

      // TODO: offset should be multiplied by 8 before use and divided by 8 before stored,
      // this will allow us to maintain a full 48 bit address space
      uint64_t offset : 46;
      uint64_t cache : 2;
      uint64_t type : 1;

      friend bool operator!=(const object_location& a, const object_location& b)
      {
         return a.offset != b.offset || (a.cache != b.cache | a.type != b.type);
      }
   };

   /**
    * Assignes unique ids to objects, tracks their reference counts,
    * and their location. 
    */
   class object_db
   {
     public:
      using object_id = triedent::object_id;

      object_db(std::filesystem::path idfile, bool allow_write);
      static void create(std::filesystem::path idfile, uint64_t max_id);

      object_id alloc(object_location loc = {.offset = 0, .cache = 0});

      void                                 retain(object_id id);
      std::pair<object_location, uint16_t> release(object_id id);

      uint16_t ref(object_id id);

      object_location get(object_id id);
      object_location get(object_id id, uint16_t& ref);
      bool            set(object_id id, object_location loc);
      inline bool set(object_id id, uint64_t offset, uint64_t cache, object_location::object_type t)
      {
         return set(id, object_location{.offset = offset, .cache = cache, .type = t});
      }

      void print_stats();
      void validate(object_id i)
      {
         if (i.id > _header->first_unallocated.id)
            throw std::runtime_error("invalid object id discovered: " + std::to_string(i.id));
      }

      /**
       * Sets all non-zero refs to c
       */
      void reset_all_ref_counts(uint16_t c)
      {
         for (auto& o : _header->objects)
         {
            auto i = o.load();
            if (i & ref_count_mask)
            {
               i &= ~ref_count_mask;
               i |= c & ref_count_mask;
               o.store(i);
            }
         }
      }
      void adjust_all_ref_counts(int16_t c)
      {
         for (auto& o : _header->objects)
         {
            auto i = o.load();
            if (i & ref_count_mask)
               o.store(i + c);
         }
      }

     private:
      static constexpr uint64_t ref_count_mask = (1ull << 15) - 1;

      // clang-format off
      auto extract_offset(uint64_t x)     { return x >> 18; }
      auto extract_cache(uint64_t x)      { return (x >> 16) & 0x3; }
      auto extract_type(uint64_t x)       { return (x >> 15) & 1; }
      auto extract_ref(uint64_t x)        { return x & ((1ull << 15) - 1); }
      auto extract_next_ptr(uint64_t x)   { return x >> 15; }
      auto create_next_ptr(uint64_t x)    { return x << 15; }
      // clang-format on

      inline uint64_t obj_val(object_location loc, uint16_t ref)
      {
         return uint64_t(loc.offset << 18) | (uint64_t(loc.cache) << 16) |
                (uint64_t(loc.type) << 15) | int64_t(ref);
      }

      struct object_db_header
      {
         std::atomic<uint64_t> first_free;         // free list
         object_id             first_unallocated;  // high water mark
         object_id             max_unallocated;    // end of file

         std::atomic<uint64_t> objects[1];
      };

      std::unique_ptr<bip::file_mapping>  _file;
      std::unique_ptr<bip::mapped_region> _region;

      object_db_header* _header;
   };

   inline void object_db::create(std::filesystem::path idfile, uint64_t max_id)
   {
      if (std::filesystem::exists(idfile))
         throw std::runtime_error("file already exists: " + idfile.generic_string());

      std::cerr << "creating " << idfile << std::endl;
      {
         std::ofstream out(idfile.generic_string(), std::ofstream::trunc);
         out.close();
      }
      auto idfile_size = sizeof(object_db_header) + max_id * 8;
      std::filesystem::resize_file(idfile, idfile_size);

      bip::file_mapping  fm(idfile.generic_string().c_str(), bip::read_write);
      bip::mapped_region mr(fm, bip::read_write, 0, sizeof(object_db_header));

      auto header                  = reinterpret_cast<object_db_header*>(mr.get_address());
      header->first_unallocated.id = 0;
      header->first_free.store(0);
      header->max_unallocated.id = (idfile_size - sizeof(object_db_header)) / 8;
   }

   inline object_db::object_db(std::filesystem::path idfile, bool allow_write)
   {
      if (not std::filesystem::exists(idfile))
         throw std::runtime_error("file does not exist: " + idfile.generic_string());

      auto existing_size = std::filesystem::file_size(idfile);

      std::cerr << "mapping '" << idfile << "' in "  //
                << (allow_write ? "read/write" : "read only") << " mode\n";

      auto mode = allow_write ? bip::read_write : bip::read_only;

      _file = std::make_unique<bip::file_mapping>(  //
          idfile.generic_string().c_str(), mode);

      _region = std::make_unique<bip::mapped_region>(*_file, mode);

      if (mlock(_region->get_address(), existing_size) < 0)
         throw std::runtime_error("unable to lock memory for " + idfile.generic_string());

      _header = reinterpret_cast<object_db_header*>(_region->get_address());

      if (_header->max_unallocated.id != (existing_size - sizeof(object_db_header)) / 8)
         throw std::runtime_error("file corruption detected: " + idfile.generic_string());
   }

   inline object_db::object_id object_db::alloc(object_location loc)
   {
      if (_header->first_unallocated.id >= _header->max_unallocated.id)
         throw std::runtime_error("no more object ids");

      if (_header->first_free.load() == 0)
      {
         ++_header->first_unallocated.id;
         auto  r   = _header->first_unallocated;
         auto& obj = _header->objects[r.id];
         obj.store(obj_val(loc, 1));  // init ref count 1
         assert(r.id != 0);
         return r;
      }
      else
      {
         uint64_t ff = _header->first_free.load();
         while (not _header->first_free.compare_exchange_strong(
             ff, extract_next_ptr(_header->objects[ff].load())))
         {
         }

         _header->objects[ff].store(obj_val(loc, 1));  // init ref count 1
         return {.id = ff};
      }
   }
   inline void object_db::retain(object_id id)
   {
      assert(id.id <= _header->first_unallocated.id);
      if (id.id > _header->first_unallocated.id) [[unlikely]]
         throw std::runtime_error("invalid object id, outside allocated range");

      auto& obj = _header->objects[id.id];
      assert(ref(id) > 0);
      assert(ref(id) != ref_count_mask);

      /*
      auto cur_ref = obj.load() & ref_count_mask;

      if( cur_ref == 0 )[[unlikely]]  
         throw std::runtime_error( "cannot retain an object at 0" );
      
      if( cur_ref == ref_count_mask )[[unlikely]] 
         throw std::runtime_error( "too many references" );
         */

      obj.fetch_add(1);
   }

   /**
    *  Return null object_location if not released, othewise returns the location
    *  that was freed
    */
   inline std::pair<object_location, uint16_t> object_db::release(object_id id)
   {
      auto& obj       = _header->objects[id.id];
      auto  val       = obj.fetch_sub(1) - 1;
      auto  new_count = (val & ref_count_mask);

      assert(new_count != ref_count_mask);
      //   if( new_count == ref_count_mask )[[unlikely]] {
      //      WARN( "id: ", id.id );
      //      assert( !"somethign went wrong with ref, released ref count of 0" );
      //      throw std::runtime_error( "something went wrong with ref counts" );
      //   }
      if (new_count == 0)
      {
         // the invariant is first_free->object with id that points to next free
         // 1. update object to point to next free
         // 2. then attempt to update first free
         uint64_t ff;
         do
         {
            ff = _header->first_free.load();
            obj.store(create_next_ptr(ff));
         } while (not _header->first_free.compare_exchange_strong(ff, id.id));
      }
      return {object_location{.offset = extract_offset(val),
                              .cache  = extract_cache(val),
                              .type   = extract_type(val)},
              new_count};
   }

   inline uint16_t object_db::ref(object_id id)
   {
      return _header->objects[id.id].load() & ref_count_mask;
   }

   inline object_location object_db::get(object_id id)
   {
      auto val = _header->objects[id.id].load();
      //    std::atomic_thread_fence(std::memory_order_acquire);

      assert((val & ref_count_mask) or !"expected positive ref count");
      if (not(val & ref_count_mask)) [[unlikely]]  // TODO: remove in release
         throw std::runtime_error("expected positive ref count");
      object_location r;
      r.cache  = extract_cache(val);
      r.offset = extract_offset(val);
      r.type   = extract_type(val);
      //  std::atomic_thread_fence(std::memory_order_acquire);
      return r;
   }

   inline object_location object_db::get(object_id id, uint16_t& ref)
   {
      auto val = _header->objects[id.id].load();

      // if( not (val & ref_count_mask) ) // TODO: remove in release
      //    throw std::runtime_error("expected positive ref count");

      //  std::atomic_thread_fence(std::memory_order_acquire);
      // assert((val & 0xffff) or !"expected positive ref count");
      object_location r;
      r.cache  = extract_cache(val);
      r.offset = extract_offset(val);
      r.type   = extract_type(val);
      ref      = extract_ref(val);
      //  std::atomic_thread_fence(std::memory_order_acquire);
      return r;
   }

   inline bool object_db::set(object_id id, object_location loc)
   {
      auto&    obj = _header->objects[id.id];
      auto     old = obj.load();
      uint16_t ref = old & ref_count_mask;
      auto     r   = obj.compare_exchange_strong(old, obj_val(loc, ref));
      //         std::atomic_thread_fence(std::memory_order_release);
      return r;
   }
   inline void object_db::print_stats()
   {
      uint64_t zero_ref     = 0;
      uint64_t total        = 0;
      uint64_t non_zero_ref = 0;
      auto*    ptr          = _header->objects;
      auto*    end          = _header->objects + _header->max_unallocated.id;
      while (ptr != end)
      {
         zero_ref += 0 == (ptr->load() & ref_count_mask);
         ++total;
         ++ptr;
      }
      std::cerr << std::setw(10) << std::left << "obj ids"
                << "|";
      std::cerr << std::setw(12) << std::left << (" " + std::to_string(total - zero_ref)) << "|";
      std::cerr << std::setw(12) << std::left << (" " + std::to_string(zero_ref)) << "|";
      std::cerr << std::setw(12) << std::left << (" " + std::to_string(total)) << "|";
      std::cerr << std::endl;
      /*
      DEBUG("first unallocated  ", _header->first_unallocated.id);
      DEBUG("total objects: ", total, " zero ref: ", zero_ref, "  non zero: ", total - zero_ref);
      */
   }

};  // namespace triedent
