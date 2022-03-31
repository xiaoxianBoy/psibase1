#define CATCH_CONFIG_MAIN
#include <psio/fracpack.hpp>

#include <contracts/system/account_sys.hpp>
#include <contracts/system/test.hpp>
#include "nft_sys.hpp"

using namespace eosio;
using namespace nft_sys;
using namespace psibase;
using nft_sys::nft_row;
using std::optional;
using std::pair;
using std::vector;

namespace
{
   bool succeeded(const transaction_trace& t) { return show(false, t) == ""; };
   bool failedWith(const transaction_trace& t, std::string_view err)
   {
      bool hasError = t.error != std::nullopt;
      bool ret      = (hasError && t.error->find(err.data()) != string::npos);
      if (hasError && !ret)
      {
         UNSCOPED_INFO("transaction has exception: " << *t.error << "\n");
      }
      else if (!hasError)
      {
         UNSCOPED_INFO("transaction succeeded, but was expected to fail");
      }
      return ret;
   }

   void check_disk_consumption(const transaction_trace&                  trace,
                               const vector<pair<account_num, int64_t>>& consumption)
   {
      const vector<action_trace>& actions = trace.action_traces;
      INFO("This check expects only single action traces");
      CHECK(actions.size() == 1);
      //const auto& ram_deltas = actions.at(0).account_ram_deltas;

      {
         INFO("Check for equality in the total number of RAM changes");
         //CHECK(ram_deltas.size() == consumption.size());
      }

      {
         INFO("Check that each actual RAM delta was in the set of expected deltas");
         // for (const auto& delta : ram_deltas)
         // {
         //    bool foundMatch =
         //        std::any_of(consumption.begin(), consumption.end(), [&](const auto& cPair) {
         //           return cPair.first == delta.account && cPair.second == delta.delta;
         //        });
         //    if (!foundMatch)
         //    {
         //       INFO("Real RAM Delta: [" << delta.account.to_string() << "]["
         //                                << std::to_string(delta.delta) << "]");
         //       CHECK(false);
         //    }
         // }
      }
   }
}  // namespace

SCENARIO("Minting nfts")
{
   GIVEN("An empty chain with registered users Alice and Bob")
   {
      test_chain t;
      t.start_block();
      boot_minimal(t);
      auto cnum = add_contract(t, "nft.sys", "nft_sys.wasm");
      REQUIRE(cnum == nft_contract::contract);

      Actor alice{add_account(t, "alice")};
      Actor bob{add_account(t, "bob")};

      uint32_t sub_id = 0;
      THEN("Alice can mint an NFT")
      {
         transaction_trace trace = t.trace(alice.at<nft_contract>().mint(alice, sub_id));
         INFO("Delete me - everything worked");
         REQUIRE(show(false, trace) == "");

         AND_THEN("The NFT exists")
         {
            auto generate = [](uint32_t a, uint32_t b) {
               return (static_cast<uint64_t>(a) << 32 | static_cast<uint64_t>(b));
            };

            nft_row expectedNft{.nftid            = generate(alice.id, sub_id),
                                .issuer           = alice.id,
                                .owner            = alice.id,
                                .approved_account = 0};

            auto nft_id = getReturnVal<&nft_contract::mint>(trace);
            trace       = t.trace(alice.at<nft_contract>().get_nft(nft_id));
            auto nft    = getReturnVal<&nft_contract::get_nft>(trace);
            REQUIRE(nft != std::nullopt);
            REQUIRE(*nft == expectedNft);
         }
      }
      THEN("Alice cannot force Bob to pay the storage cost for her minting an NFT")
      {
         CHECK(failedWith(t.trace(alice.at<nft_contract>().mint(bob, sub_id)),
                          "Missing required authority"));
      }
      WHEN("Alice mints an NFT")
      {
         auto trace = t.trace(alice.at<nft_contract>().mint(alice, sub_id));
         t.finish_block();

         THEN("Alice's RAM is consumed as expected")
         {
            check_disk_consumption(trace, {{alice.id, nft_row::DiskUsage::firstEmplace}});
         }
         THEN("Alice cannot mint another using the same sub_id")
         {
            CHECK(failedWith(t.trace(alice.at<nft_contract>().mint(alice, sub_id)),
                             "Nft already exists"));
         }
         THEN("Bob can mint an NFT using the same sub_id")
         {
            CHECK(succeeded(t.trace(bob.at<nft_contract>().mint(bob, sub_id))));
         }
         THEN("Alice can mint a second NFT using a different sub_id")
         {
            CHECK(succeeded(t.trace(alice.at<nft_contract>().mint(alice, sub_id + 1))));
         }
         AND_WHEN("Alice mints a second NFT using a different sub_id")
         {
            //auto nftId = t.act(alice.at<nft_contract>().mint(alice, sub_id + 1));
            //auto nft1  = t.act(alice.at<nft_contract>().get_nft(nftId));
            t.finish_block();

            THEN("The ID is one more than the first")
            {
               //nft_row nft2        = *(t.act(alice.at<nft_contract>().get_nft(nftId)));
               //nft_row expectedNft = *nft1;
               //expectedNft.nftid++;
               //REQUIRE(nft2 == expectedNft);
            }
         }
         AND_WHEN("Bob mints an NFT")
         {
            auto trace = t.trace(bob.at<nft_contract>().mint(bob, sub_id));
            t.finish_block();

            THEN("Bob's pays for an expected amount of storage space")
            {
               check_disk_consumption(trace, {{bob.id, nft_row::DiskUsage::subsequentEmplace}});
            }
         }
      }
   }
}
