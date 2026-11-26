#include <cstdio>
#include <cstddef>
#include <cstdint>
int main() {
    std::printf("%d %d %d %d | %d %d %d %d\n", (int)sizeof(std::int_least8_t), (int)sizeof(std::int_least16_t), (int)sizeof(std::int_least32_t), (int)sizeof(std::int_least64_t),
                (int)sizeof(std::uint_least8_t), (int)sizeof(std::uint_least16_t), (int)sizeof(std::uint_least32_t), (int)sizeof(std::uint_least64_t));
    std::printf("%d %d %d %d | %d %d %d %d\n", (int)sizeof(std::int_fast8_t), (int)sizeof(std::int_fast16_t), (int)sizeof(std::int_fast32_t), (int)sizeof(std::int_fast64_t),
                (int)sizeof(std::uint_fast8_t), (int)sizeof(std::uint_fast16_t), (int)sizeof(std::uint_fast32_t), (int)sizeof(std::uint_fast64_t));
    std::printf("%d %d %lld %lld %llu\n", INT_LEAST8_MIN, INT_LEAST16_MAX, (long long)INT_FAST16_MAX, (long long)INT_FAST64_MIN, (unsigned long long)UINT_FAST32_MAX);
    std::int_least8_t l8 = -1;
    std::uint_fast8_t f8 = 255;
    std::printf("%d %d %d %d\n", (int)l8, (int)f8, (int)sizeof(std::max_align_t), (int)alignof(std::max_align_t) >= (int)alignof(double));
    return 0;
}
