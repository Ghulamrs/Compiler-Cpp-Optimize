#include <cstdio>
#include <type_traits>
int main() {
    std::printf("%d %d %d %d\n", (int)std::is_volatile<volatile int>::value, (int)std::is_volatile<int>::value,
                (int)std::is_volatile<const volatile char>::value, (int)std::is_volatile<volatile int *>::value);
    std::printf("%d %d %d\n", (int)std::is_same<std::remove_volatile<volatile int>::type, int>::value,
                (int)std::is_same<std::remove_cv<const volatile int>::type, int>::value,
                (int)std::is_same<std::add_cv<int>::type, const volatile int>::value);
    std::printf("%d %d\n", (int)std::is_same<std::add_volatile<int>::type, volatile int>::value,
                (int)std::is_same<std::remove_pointer<int *volatile>::type, int>::value);
    return 0;
}
