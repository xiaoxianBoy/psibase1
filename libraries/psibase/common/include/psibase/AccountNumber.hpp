#pragma once
#include <compare>
#include <eosio/reflection.hpp>
#include <psibase/name.hpp>
#include <psio/from_json.hpp>
#include <psio/to_json.hpp>

// Todo - remove when kv table uses PSIO
#include <eosio/reflection.hpp>

namespace psibase
{
   struct AccountNumber final
   {
      uint64_t value = 0;
      constexpr AccountNumber() : value(0) {}
      constexpr explicit AccountNumber(uint64_t v) : value(v) {}
      constexpr explicit AccountNumber(std::string_view s) : value(name_to_number(s)) {}
      std::string str() const { return number_to_name(value); }
      auto        operator<=>(const AccountNumber&) const = default;
   };
   PSIO_REFLECT(AccountNumber, value)
   EOSIO_REFLECT(AccountNumber, value)  //Todo - remove when kv table uses PSIO

   // TODO: remove
   using account_num = AccountNumber;

   template <typename S>
   void to_json(const AccountNumber& n, S& s)
   {
      to_json(n.str(), s);
   }

   template <typename S>
   void from_json(AccountNumber& result, S& stream)
   {
      result = AccountNumber{stream.get_string()};
   }

   inline constexpr bool use_json_string_for_gql(AccountNumber*) { return true; }

}  // namespace psibase

// TODO: move to psibase::literals (inline namespace)
inline constexpr psibase::AccountNumber operator""_a(const char* s, unsigned long)
{
   auto num = psibase::AccountNumber(s);
   if (not num.value)
   {
      std::abort();  // failed_to_compress_name
   }
   return num;
}
