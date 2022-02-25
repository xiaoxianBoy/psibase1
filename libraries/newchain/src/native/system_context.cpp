#include <newchain/action_context.hpp>

#include <mutex>

namespace newchain
{
   struct shared_state_impl
   {
      std::mutex                                   mutex;
      std::unique_ptr<database>                    db;
      newchain::wasm_cache                         wasm_cache;
      std::vector<std::unique_ptr<system_context>> system_context_cache;

      shared_state_impl(std::unique_ptr<database> db, newchain::wasm_cache wasm_cache)
          : db{std::move(db)}, wasm_cache{std::move(wasm_cache)}
      {
      }
   };

   shared_state::shared_state(std::unique_ptr<database> db, newchain::wasm_cache wasm_cache)
       : impl{std::make_unique<shared_state_impl>(std::move(db), std::move(wasm_cache))}
   {
   }

   shared_state::~shared_state() {}

   std::unique_ptr<system_context> shared_state::get_system_context()
   {
      std::lock_guard<std::mutex> lock{impl->mutex};
      if (impl->system_context_cache.empty())
         return std::make_unique<system_context>(system_context{*impl->db, impl->wasm_cache});
      auto result = std::move(impl->system_context_cache.back());
      impl->system_context_cache.pop_back();
      return result;
   }

   void shared_state::add_system_context(std::unique_ptr<system_context> context)
   {
      std::lock_guard<std::mutex> lock{impl->mutex};
      impl->system_context_cache.push_back(std::move(context));
   }
}  // namespace newchain
