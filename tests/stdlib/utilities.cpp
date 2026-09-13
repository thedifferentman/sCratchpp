#include <memory>
#include <functional>
#include <optional>
#include <variant>
#include <tuple>
#include <type_traits>
#include <string>
#include <cstring>
#include <utility>

struct Counted { static int live; int value; explicit Counted(int v) : value(v) { ++live; } ~Counted() { --live; } };
int Counted::live;
static_assert(std::is_same_v<std::tuple_element_t<0, std::tuple<int, char>>, int>);
static_assert(std::is_same_v<decltype(std::strchr(std::declval<const char*>(), 'x')), const char*>);
static_assert(std::is_same_v<decltype(std::strchr(std::declval<char*>(), 'x')), char*>);
static_assert(std::is_same_v<decltype(std::strrchr(std::declval<const char*>(), 'x')), const char*>);
static_assert(std::is_same_v<decltype(std::strrchr(std::declval<char*>(), 'x')), char*>);
static_assert(std::is_same_v<decltype(std::strpbrk(std::declval<const char*>(), "x")), const char*>);
static_assert(std::is_same_v<decltype(std::strpbrk(std::declval<char*>(), "x")), char*>);
static_assert(std::is_same_v<decltype(std::strstr(std::declval<const char*>(), "x")), const char*>);
static_assert(std::is_same_v<decltype(std::strstr(std::declval<char*>(), "x")), char*>);
static_assert(std::is_same_v<decltype(std::memchr(std::declval<const void*>(), 'x', 1)), const void*>);
static_assert(std::is_same_v<decltype(std::memchr(std::declval<void*>(), 'x', 1)), void*>);

int main() {
    {
        auto u = std::make_unique<Counted>(7);
        auto moved = std::move(u);
        if (u || moved->value != 7 || Counted::live != 1) return 1;
        auto s = std::make_shared<Counted>(8);
        std::weak_ptr<Counted> weak = s;
        {
            auto copy = weak.lock();
            if (s.use_count() != 2 || copy->value != 8) return 2;
        }
        s.reset();
        if (!weak.expired() || Counted::live != 1) return 3;
    }
    if (Counted::live) return 4;
    std::optional<int> opt = 11;
    if (opt.value_or(4) != 11) return 5;
    opt.reset();
    if (opt.value_or(4) != 4) return 6;
    std::variant<int, std::string> var = std::string("value");
    if (std::get<std::string>(var) != "value") return 7;
    var = 12;
    if (!std::holds_alternative<int>(var) || std::get<int>(var) != 12) return 8;
    std::function<int(int)> fn = [offset = 3](int x) { return offset + x; };
    auto fn_copy = fn;
    if (fn_copy(5) != 8) return 9;
    auto tuple = std::make_tuple(9, 'a');
    if (std::get<0>(tuple) != 9 || std::get<1>(tuple) != 'a') return 10;
    return 0;
}
