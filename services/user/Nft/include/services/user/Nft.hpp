#pragma once

#include <psibase/Memo.hpp>
#include <psibase/psibase.hpp>

#include <services/system/CommonTables.hpp>
#include <services/user/nftErrors.hpp>
#include <services/user/nftTables.hpp>

namespace UserService
{
   class Nft : public psibase::Service<Nft>
   {
     public:
      using Tables = psibase::ServiceTables<NftTable, NftHolderTable, CreditTable, InitTable>;

      static constexpr auto service = psibase::AccountNumber("nft");

      Nft(psio::shared_view_ptr<psibase::Action> action);

      void init();
      NID  mint();
      void burn(NID nftId);
      void credit(NID nftId, psibase::AccountNumber receiver, psio::view<const psibase::Memo> memo);
      void uncredit(NID nftId, psio::view<const psibase::Memo> memo);
      void debit(NID nftId, psio::view<const psibase::Memo> memo);
      void setUserConf(psibase::EnumElement flag, bool enable);

      std::optional<psibase::HttpReply> serveSys(psibase::HttpRequest request);

      // Read-only:
      NftRecord       getNft(NID nftId);
      NftHolderRecord getNftHolder(psibase::AccountNumber account);
      CreditRecord    getCredRecord(NID nftId);
      bool            exists(NID nftId);
      bool            getUserConf(psibase::AccountNumber account, psibase::EnumElement flag);

     public:
      struct Events
      {
         using Account  = psibase::AccountNumber;
         using MemoView = psio::view<const psibase::Memo>;
         // clang-format off
         struct History
         {
            void minted(uint64_t prevEvent, NID nftId, Account issuer) {}
            void burned(uint64_t prevEvent, NID nftId) {}
            void userConfSet(uint64_t prevEvent, Account account, psibase::EnumElement flag, bool enable) {}
            void credited(uint64_t prevEvent, NID nftId, Account sender, Account receiver, MemoView memo) {}
            void uncredited(uint64_t prevEvent, NID nftId, Account sender, Account receiver, MemoView memo) {}
            void transferred(uint64_t prevEvent, NID nftId, Account creditor, Account debitor, MemoView memo) {}
         };
         // clang-format on

         struct Ui
         {
         };
         struct Merkle
         {
         };
      };
      using NftEvents  = psibase::EventIndex<&NftRecord::eventHead, "prevEvent">;
      using UserEvents = psibase::EventIndex<&NftHolderRecord::eventHead, "prevEvent">;
   };

   // clang-format off
   PSIO_REFLECT(Nft,
      method(init),
      method(mint),
      method(burn, nftId),
      method(credit, nftId, receiver, memo),
      method(uncredit, nftId, memo),
      method(debit, nftId, memo),
      method(setUserConf, flag, enable),
      method(serveSys, request),

      method(getNft, nftId),
      method(getNftHolder, account),
      method(getCredRecord, nftId),
      method(exists, nftId),
      method(getUserConf, account, flag)
   );
   PSIBASE_REFLECT_EVENTS(Nft);
   PSIBASE_REFLECT_HISTORY_EVENTS(Nft,
      method(minted, prevEvent, nftId, issuer),
      method(burned, prevEvent, nftId),
      method(userConfSet, prevEvent, account, flag, enable),
      method(credited, prevEvent, nftId, sender, receiver, memo),
      method(uncredited, prevEvent, nftId, sender, receiver, memo),
      method(transferred, prevEvent, nftId, creditor, debitor, memo)
   );
   PSIBASE_REFLECT_UI_EVENTS(Nft);
   PSIBASE_REFLECT_MERKLE_EVENTS(Nft);

   // clang-format on

}  // namespace UserService
