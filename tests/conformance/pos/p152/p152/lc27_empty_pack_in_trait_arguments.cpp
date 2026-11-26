#include <cstdio>
#include <memory>
#include <type_traits>
typedef std::allocator<int> A;
struct First {
    typedef A allocator_type;
    First() {}
    First(std::allocator_arg_t, const A &) {}
};
template <class T, class Al, class... Args> struct K
    : std::integral_constant<int, std::is_constructible<T, std::allocator_arg_t, const Al &, Args...>::value ? 1 : 2> {};
template <class T, class Al, class... Args> struct K2
    : std::integral_constant<int, std::is_constructible<T, std::allocator_arg_t, const Al &>::value ? 1 : 2> {};
int main() {
    std::printf("%d %d %d %d\n", (int)std::is_constructible<First, std::allocator_arg_t, const A &>::value,
                K<First, A>::value, K2<First, A>::value, (int)std::uses_allocator<First, A>::value);
}
