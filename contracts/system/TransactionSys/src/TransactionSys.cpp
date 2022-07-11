#include <contracts/system/AuthFakeSys.hpp>
#include <contracts/system/TransactionSys.hpp>
#include <psibase/dispatch.hpp>

#include <psibase/crypto.hpp>
#include <psibase/nativeTables.hpp>
#include <psibase/print.hpp>

using namespace psibase;

static constexpr bool enable_print = false;

namespace system_contract
{
   struct IncludedTrx
   {
      Checksum256  id;
      TimePointSec expiration;

      auto by_expiration() const { return std::make_tuple(expiration, id); }
   };
   PSIO_REFLECT(IncludedTrx, id, expiration)
   using IncludedTrxTable = Table<IncludedTrx, &IncludedTrx::id, &IncludedTrx::by_expiration>;

   using Tables = ContractTables<IncludedTrxTable>;

   static const auto& getStatus()
   {
      static const auto status = kvGet<StatusRow>(StatusRow::db, statusKey());
      return status;
   }

   void TransactionSys::startBlock()
   {
      // TODO: expire transaction IDs
      // TODO: record (and expire) information for tapos
   }

   psibase::BlockNum TransactionSys::headBlockNum() const
   {
      auto& stat = getStatus();
      if (stat && stat->head)
         return stat->head->header.blockNum;
      return 1;  // next block (currently being produced) is 2 (genesis)
   }
   psibase::TimePointSec TransactionSys::headBlockTime() const
   {
      auto& stat = getStatus();
      if (stat && stat->head)
         return stat->head->header.time;
      return {};
   }

   // TODO: move to another contract
   uint8_t TransactionSys::setCode(AccountNumber     contract,
                                   uint8_t           vmType,
                                   uint8_t           vmVersion,
                                   std::vector<char> code)
   {
      // TODO: validate code
      check(getSender() == contract, "sender must match contract account");
      auto account = kvGet<AccountRow>(AccountRow::db, accountKey(contract));
      check(account.has_value(), "can not set code on a missing account");
      auto codeHash = sha256(code.data(), code.size());
      if (vmType == account->vmType && vmVersion == account->vmVersion &&
          codeHash == account->codeHash)
         return 0;
      if (account->codeHash != Checksum256{})
      {
         // TODO: Refund RAM? A different resource?
         auto code_obj = kvGet<codeRow>(
             codeRow::db, codeKey(account->codeHash, account->vmType, account->vmVersion));
         check(code_obj.has_value(), "missing code object");
         if (--code_obj->numRefs)
            kvPut(code_obj->db, code_obj->key(), *code_obj);
         else
         {
            // TODO: erase code_obj
         }
      }

      // TODO: Bill RAM? A different resource?
      account->codeHash  = codeHash;
      account->vmType    = vmType;
      account->vmVersion = vmVersion;
      kvPut(account->db, account->key(), *account);

      auto code_obj = kvGet<codeRow>(
          codeRow::db, codeKey(account->codeHash, account->vmType, account->vmVersion));
      if (!code_obj)
      {
         code_obj.emplace(codeRow{
             .codeHash  = account->codeHash,
             .vmType    = account->vmType,
             .vmVersion = account->vmVersion,
         });
         code_obj->code.assign(code.begin(), code.end());
      }
      ++code_obj->numRefs;
      kvPut(code_obj->db, code_obj->key(), *code_obj);

      return 0;
   }  // setCode

   // Native code calls this on the transaction-sys account
   extern "C" [[clang::export_name("process_transaction")]] void process_transaction()
   {
      if (enable_print)
         print("process_transaction\n");

      // TODO: check refBlockNum, refBlockPrefix
      // TODO: check max_net_usage_words, max_cpu_usage_ms
      // TODO: resource billing
      // TODO: subjective mitigation hooks
      // TODO: limit execution time
      auto top_act = getCurrentAction();
      // TODO: avoid copying inner rawData during unpack
      // TODO: verify fracpack (no unknown, no extra data)
      auto trx = psio::convert_from_frac<Transaction>(top_act.rawData);
      auto id  = sha256(top_act.rawData.data(), top_act.rawData.size());

      check(trx.actions.size() > 0, "transaction has no actions");

      const auto& stat = getStatus();
      check(stat.has_value(), "missing status record");
      check(stat->current.time <= trx.tapos.expiration, "transaction has expired");

      auto table = Tables(TransactionSys::contract).open<IncludedTrxTable>();
      auto idx   = table.getIndex<0>();
      check(!idx.get(id), "duplicate transaction");
      table.put({id, trx.tapos.expiration});

      for (auto& act : trx.actions)
      {
         auto account = kvGet<AccountRow>(AccountRow::db, accountKey(act.sender));
         if (!account)
            abortMessage("unknown sender \"" + act.sender.str() + "\"");

         if (enable_print)
            print("call checkAuthSys on ", account->authContract.str(), " for account ",
                  act.sender.str());
         Actor<AuthInterface> auth(TransactionSys::contract, account->authContract);
         auth.checkAuthSys(act, trx.claims);
         if (enable_print)
            print("call action\n");
         call(act);  // TODO: avoid copy (serializing)
      }
   }

}  // namespace system_contract

PSIBASE_DISPATCH(system_contract::TransactionSys)
