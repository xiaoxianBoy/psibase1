#pragma once
#include <psibase/actor.hpp>
#include <psibase/intrinsic.hpp>
#include <psibase/name.hpp>
#include <psibase/native_tables.hpp>

namespace system_contract
{
   class account_sys : public psibase::contract
   {
     public:
      static constexpr auto     contract       = psibase::AccountNumber("account-sys");
      static constexpr uint64_t contract_flags = psibase::account_row::allow_write_native;
      static constexpr psibase::AccountNumber null_account = psibase::AccountNumber(0);

      void startup(psio::const_view<std::vector<psibase::AccountNumber>> existing_accounts);
      void newAccount(psibase::AccountNumber account,
                      psibase::AccountNumber auth_contract,
                      bool                   allow_sudo);
      bool exists(psibase::AccountNumber num);
   };

   PSIO_REFLECT(account_sys,
                method(startup, existing_accounts),
                method(newAccount, name, auth_contract, allow_sudo),
                method(exists, num))

}  // namespace system_contract
