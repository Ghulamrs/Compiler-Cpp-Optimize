#include <cstdio>
#include <memory>
#include <tuple>
#include <utility>
typedef std::allocator<int> A;
struct First {
    typedef A allocator_type;
    int v, how;
    First() : v(0), how(0) {}
    First(std::allocator_arg_t, const A &) : v(0), how(1) {}
    First(std::allocator_arg_t, const A &, int x) : v(x), how(1) {}
    First(std::allocator_arg_t, const A &, const First &o) : v(o.v), how(1) {}
    First(int x) : v(x), how(0) {}
};
struct Last {
    typedef A allocator_type;
    int v, how;
    Last() : v(0), how(0) {}
    explicit Last(const A &) : v(0), how(2) {}
    Last(int x, const A &) : v(x), how(2) {}
    Last(const Last &o, const A &) : v(o.v), how(2) {}
    Last(int x) : v(x), how(0) {}
};
struct None {
    int v;
    None() : v(0) {}
    None(int x) : v(x) {}
};
int main() {
    A a;
    std::tuple<First, Last, None> t(std::allocator_arg, a);
    std::printf("%d %d %d\n", std::get<0>(t).how, std::get<1>(t).how, std::get<2>(t).v);
    std::tuple<First, Last, None> u(std::allocator_arg, a, 1, 2, 3);
    std::printf("%d %d %d %d %d\n", std::get<0>(u).v, std::get<0>(u).how, std::get<1>(u).v, std::get<1>(u).how, std::get<2>(u).v);
    std::tuple<First, Last, None> w(std::allocator_arg, a, u);
    std::printf("%d %d %d\n", std::get<0>(w).v + std::get<1>(w).v + std::get<2>(w).v, std::get<0>(w).how, std::get<1>(w).how);
    std::tuple<int, char> p(std::allocator_arg, a, std::make_pair(7, 'q'));
    std::printf("%d %c %d\n", std::get<0>(p), std::get<1>(p), (int)std::uses_allocator<std::tuple<int>, A>::value);
    return 0;
}
