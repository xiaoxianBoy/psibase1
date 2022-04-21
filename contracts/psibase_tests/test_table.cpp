#include <psibase/table.hpp>
#include <psio/to_json.hpp>
#include <variant>

using namespace psibase;

namespace table_test
{
   struct S0
   {
      int                         key;
      int                         value;
      friend std::strong_ordering operator<=>(const S0&, const S0&) = default;
   };
   EOSIO_REFLECT(S0, key, value)
   PSIO_REFLECT(S0, key, value)
   using table0 = table<S0, &S0::key>;

   struct S1
   {
      int                         key1;
      int                         key2;
      int                         value;
      friend std::strong_ordering operator<=>(const S1&, const S1&) = default;
   };
   EOSIO_REFLECT(S1, key1, key2, value);
   PSIO_REFLECT(S1, key1, key2, value);
   using table1 = table<S1, &S1::key1, &S1::key2>;

   struct S2
   {
      int                         key1;
      int                         key2;
      int                         value;
      auto                 compound_key()const  { 
         return std::tie(key2, key1); 
      }
      /*
      static auto                 compound_key( const_view<S2> v ) { 
         return std::tuple(v->key1(), v->key2() ); 
      }
      */
      friend std::strong_ordering operator<=>(const S2&, const S2&) = default;
   };
   EOSIO_REFLECT(S2, key1, key2, value, compound_key);
   using table2 = table<S2, &S2::key1>;

   using test_tables = contract_tables<table0, table1, table2>;

   void test0(account_num this_contract)
   {
      test_tables t{this_contract};
      auto        t0   = t.open<table0>();
      auto        idx0 = t0.get_index<0>();
      t0.put(S0{0, 1});
      check(idx0.get(0) == S0{0, 1}, "get after create");
      t0.put(S0{0, 2});
      check(idx0.get(0) == S0{0, 2}, "get after modify");
      t0.put(S0{1, 3});
      check(idx0.get(0) == S0{0, 2} && idx0.get(1) == S0{1, 3}, "get after different key");
   }

   void test1(account_num this_contract)
   {
      test_tables t{this_contract};
      auto        t1   = t.open<table1>();
      auto        idx0 = t1.get_index<0>();
      auto        idx1 = t1.get_index<1>();
      eosio::print("start put\n");
      t1.put(S1{0, 1, 2});
      eosio::print("start get\n");
      eosio::print("primary:", psio::convert_to_json(S1{0, 1, 2}));
      eosio::print("\n idx0: ", psio::convert_to_json(idx0.get(0)));
      eosio::print("\n idx1: ", psio::convert_to_json(idx1.get(1)));
      check(idx0.get(0) == S1{0, 1, 2}, "get0");
      check(idx1.get(1) == S1{0, 1, 2}, "get1");
      t1.put(S1{0, 2, 3});
      check(idx0.get(0) == S1{0, 2, 3}, "get0 updated");
      check(idx1.get(2) == S1{0, 2, 3}, "get1 new");
      check(idx1.get(1) == std::nullopt, "get1 removed");
   }

   /*
   void test2(account_num this_contract)
   {
      test_tables t{this_contract};
      auto        t2   = t.open<table2>();
      auto        idx1 = t2.get_index<1>();
      t2.put(S2{0, 1, 2});
      t2.put(S2{3, 1, 4});
      t2.put(S2{5, 6, 7});
      std::vector<S2> result;
      printf("starting test2\n");
      for (auto x : idx1.subindex(1))
      {
         result.push_back(x);
      }
      check(result.size() == 2, "iter sz");
      check(result[0] == S2{0, 1, 2}, "iter val0");
      check(result[1] == S2{3, 1, 4}, "iter val1");
   }
   */

   extern "C" void called(account_num this_contract, account_num sender)
   {
      //   test0(this_contract);
      test1(this_contract);
      //  test2(this_contract);
   }

   extern "C" void __wasm_call_ctors();
   extern "C" void start(account_num this_contract) { __wasm_call_ctors(); }
}  // namespace table_test
