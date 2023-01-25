#include <psio/view.hpp>

#include <catch2/catch.hpp>

template <typename T, typename F>
void test_view(const T& value, F&& f)
{
   auto bin = psio::to_frac(value);
   f(psio::view<T>{bin});
   f(psio::view<const T>{bin});
   f(psio::view<const T>{psio::view<T>{bin}});
}

template <typename T, typename F>
void test_mutate(const T& original, F&& f, const T& expected)
{
   auto bin = psio::to_frac(original);
   f(psio::view<T>{bin});
   CHECK(psio::from_frac<T>(bin) == expected);
}

template <typename T, typename F>
void test_mutate(F&& f, const T& expected)
{
   test_mutate(T{}, f, expected);
}

template <typename T>
void test_assign(const T& original, const T& modified)
{
   test_mutate(
       original, [&](auto v) { v = modified; }, modified);
}

template <typename T>
void test_assign(const T& modified)
{
   test_mutate(
       T{}, [&](auto v) { v = modified; }, modified);
}

TEST_CASE("u8 view", "[view]")
{
   test_view(std::uint8_t{0xFF},
             [](auto v)
             {
                CHECK(v == 0xFF);
                CHECK(v.unpack() == 0xFF);
             });
   test_assign(std::uint8_t{0xCC});
}

TEST_CASE("tuple view", "[view]")
{
   std::tuple t{std::uint8_t(42), std::uint8_t{127}};
   test_view(t,
             [&](auto v)
             {
                CHECK(get<0>(v) == 42);
                CHECK(get<1>(v) == 127);
                CHECK(v.unpack() == t);
                auto [v0, v1] = v;
                CHECK(v0 == 42);
                CHECK(v1 == 127);
             });
   test_mutate(
       [](auto v)
       {
          get<0>(v) = 42;
          get<1>(v) = 127;
       },
       t);
}

struct struct0
{
   std::uint8_t v0;
   std::uint8_t v1;
   friend bool  operator==(const struct0&, const struct0&) = default;
};
PSIO_REFLECT(struct0, v0, v1)

TEST_CASE("struct view", "[view]")
{
   test_view(struct0{42, 127},
             [](auto v)
             {
                CHECK(v.v0() == 42);
                CHECK(v.v1() == 127);
             });
   test_mutate(
       [](auto v)
       {
          v.v0() = 0xFF;
          v.v1() = 0xCC;
       },
       struct0{0xFF, 0xCC});
}

TEST_CASE("optional view", "[view]")
{
   test_view(std::optional<std::uint8_t>{},
             [](auto v)
             {
                CHECK(!v);
                CHECK(!v.has_value());
                CHECK(v.value_or(42) == 42);
                CHECK_THROWS_AS(v.value(), std::bad_optional_access);
             });
   test_view(std::optional<std::uint8_t>{42},
             [](auto v)
             {
                CHECK(!!v);
                CHECK(v.has_value());
                CHECK(*v == 42);
                CHECK(v.value() == 42);
                CHECK(v.value_or(0xFF) == 42);
             });
   test_mutate(
       std::optional{std::uint8_t{0}}, [](auto v) { *v = 42; }, std::optional{std::uint8_t{42}});
   test_view(std::optional{struct0{42, 127}},
             [](auto v)
             {
                CHECK(v.has_value());
                CHECK(v->v0() == 42);
                CHECK(v->v1() == 127);
             });
   test_mutate(
       std::optional{struct0{}},
       [](auto v)
       {
          v->v0() = 0xCC;
          v->v1() = 0xFF;
       },
       std::optional{struct0{0xCC, 0xFF}});
}

namespace
{

   std::string make_string(const std::string& s)
   {
      return s;
   }

   std::string make_string(std::uint8_t i)
   {
      return std::to_string(i);
   }

}  // namespace

TEST_CASE("variant view", "[view]")
{
   test_view(std::variant<std::uint8_t, std::string>(std::uint8_t{42}),
             [](auto v)
             {
                CHECK(holds_alternative<std::uint8_t>(v));
                CHECK(v.index() == 0);
                CHECK(visit([](auto x) { return make_string(x); }, v) == "42");
                CHECK(get<0>(v) == 42);
                CHECK_THROWS_AS(get<1>(v), std::bad_variant_access);
                CHECK(get<std::uint8_t>(v) == 42);
                CHECK_THROWS_AS(get<std::string>(v), std::bad_variant_access);
                CHECK(*get_if<0>(v) == 42);
                CHECK(!get_if<1>(v));
                CHECK(*get_if<std::uint8_t>(v) == 42);
                CHECK(!get_if<std::string>(v));
             });
   test_mutate([](auto v) { get<0>(v) = 42; },
               std::variant<std::uint8_t, std::string>(std::uint8_t{42}));
}

TEST_CASE("vector view", "[view]")
{
   test_view(std::vector<std::uint8_t>{0, 1, 127, 255},
             [](auto v)
             {
                CHECK(v.front() == 0);
                CHECK(v.back() == 255);
                CHECK(v.size() == 4);
                CHECK(!v.empty());
                CHECK(v[0] == 0);
                CHECK(v[1] == 1);
                CHECK(v[2] == 127);
                CHECK(v[3] == 255);
                CHECK(v.at(0) == 0);
                CHECK(v.at(1) == 1);
                CHECK(v.at(2) == 127);
                CHECK(v.at(3) == 255);
                CHECK_THROWS_AS(v.at(4), std::out_of_range);
                int sum = 0;
                for (std::uint8_t elem : v)
                {
                   sum += elem;
                }
                CHECK(sum == 1 + 127 + 255);
             });
   test_mutate(
       std::vector<uint8_t>(4),
       [](auto v)
       {
          v.front()        = 1;
          v.back()         = 2;
          v[1]             = 3;
          *(v.begin() + 2) = 4;
       },
       std::vector<uint8_t>{1, 3, 4, 2});
}
