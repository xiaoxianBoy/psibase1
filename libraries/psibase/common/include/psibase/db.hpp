#pragma once

#include <psibase/block.hpp>

namespace psibase
{
   enum class DbId : uint32_t
   {
      contract,              // Contract tables
      native_constrained,    // Native tables which enforce constraints during write
      native_unconstrained,  // Native tables which don't enforce constraints during write
      subjective,            // Data that is not part of consensus
      write_only,            // Write-only during transactions. Readable during RPC,
                             //   also subjectively writable by node operator.
      block_log,             // Not available during transactions. Readable during RPC.
      event,                 // Events

      /* designed for change events built on queries most recent finalized block,
       * and for user interfaces that want to subscribe to activity. Not readable by contracts
       */
      ui_event,      // Events that are erased once block that produced them becomes final
      merkle_event,  // Events that go into merkle tree, readable for 1 hour (configurable) or finality which ever is longer
      history_event  // Events that go into long-term subjective history
   };

   struct KvResourceKey
   {
      AccountNumber contract = {};
      uint32_t      db       = {};

      friend auto operator<=>(const KvResourceKey&, const KvResourceKey&) = default;
   };
   PSIO_REFLECT(KvResourceKey, definitionWillNotChange(), contract, db)

   struct KvResourceDelta
   {
      int64_t records    = 0;
      int64_t keyBytes   = 0;
      int64_t valueBytes = 0;
   };
   PSIO_REFLECT(KvResourceDelta, definitionWillNotChange(), records, keyBytes, valueBytes)

   struct KvResourcePair
   {
      KvResourceKey   first  = {};
      KvResourceDelta second = {};

      KvResourcePair() = default;
      KvResourcePair(KvResourceKey first, KvResourceDelta second) : first{first}, second{second} {}
   };
   PSIO_REFLECT(KvResourcePair, definitionWillNotChange(), first, second)
}  // namespace psibase
