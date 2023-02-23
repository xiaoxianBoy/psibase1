#include "fuzz.hpp"
#include "test_util.hpp"

#include <triedent/database.hpp>

// the central node can take the following actions:
// - single step another node's io_context
// - advance a node's clock
// - send a message to another node
// - create a block that builds on any existing block
// - prepare any existing block
// - commit any existing block
// - generate a view change

using namespace psibase;
using namespace psibase::net;
using namespace psio;
using namespace psibase::test;

struct Network;

template <typename Derived>
using fuzz_routing = basic_fuzz_routing<Network, Derived>;

using node_type = node<null_link, fuzz_routing, bft_consensus, ForkDb>;

struct Network : NetworkBase<node_type>
{
   explicit Network(SystemContext* ctx) : NetworkBase(ctx) {}
   template <typename Engine>
   void do_step(Engine& rng)
   {
      switch (rng() % 32)
      {
         case 0:
         case 7:
         case 8:
         case 21:
         case 22:
         case 23:
         case 24:
         case 27:
         case 28:
         {
            nodes[rng() % nodes.size()]->poll_one();
            break;
         }
         case 1:
         case 9:
         case 10:
         case 14:
         case 15:
         case 17:
         case 18:
         case 19:
         case 20:
         case 29:
         case 30:
         case 31:
         {
            forward_message(rng, nodes[rng() % nodes.size()]);
            break;
         }
         case 2:
         {
            build_block(choose_block(rng));
            break;
         }
         case 3:
         case 11:
         {
            add_prepare(choose_block(rng));
            break;
         }
         case 4:
         case 12:
         {
            add_commit(choose_block(rng));
            break;
         }
         case 5:
         {
            add_view_change(rng);
            break;
         }
         case 6:
         case 13:
         case 25:
         case 26:
         {
            expire_one_timer(rng);
            break;
         }
      }
   }
};

__AFL_FUZZ_INIT();

int main(int argc, const char** argv)
{
   handleArgs(argc, argv);

   TempDatabase db;
   auto         systemContext = db.getSystemContext();
   {
      Network network(systemContext.get());
      network.add_node("alice");
      boot<BftConsensus>(network.nodes.front()->node.chain().getBlockContext(),
                         {"alice", "bob", "carol", "mallory"});
      unsigned char a[1] = {};
      bufrng        zero_rng{a, a + sizeof(a)};
      expire_one_timer(zero_rng);
      network.nodes[0]->ctx.poll();
   }
   auto initialHead  = systemContext->sharedDatabase.getHead();
   auto initialState = systemContext->sharedDatabase.createWriter()->get_top_root();
   auto initialClock = mock_clock::now();

   unsigned char* buf = __AFL_FUZZ_TESTCASE_BUF;

   while (__AFL_LOOP(1000))
   {
      int len = __AFL_FUZZ_TESTCASE_LEN;

      for (int i = 0; i < 4; ++i)
      {
         {
            auto writer = systemContext->sharedDatabase.createWriter();
            systemContext->sharedDatabase.setHead(*writer, initialHead);
            writer->set_top_root(initialState);
         }
         reset_mock_time(initialClock);

         Network network(systemContext.get());
         network.add_node("alice");
         network.add_node("bob");
         network.add_node("carol");

         try
         {
            bufrng rng{buf, buf + len};
            //std::mt19937 rng;
            while (true)
            {
               network.do_step(rng);
            }
         }
         catch (end_of_test&)
         {
         }
         // Check consistency
         for (const auto& node1 : network.nodes)
         {
            auto commit1 = node1->node.chain().commit_index();
            for (const auto& node2 : network.nodes)
            {
               auto commit2    = node2->node.chain().commit_index();
               auto min_commit = std::min(commit1, commit2);
               if (min_commit > 1)
               {
                  assert(node1->node.chain().get_block_id(min_commit) ==
                         node1->node.chain().get_block_id(min_commit));
               }
            }
         }
      }
   }
}
