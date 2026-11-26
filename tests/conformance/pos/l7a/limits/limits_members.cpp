#include <cstdio>
#include <limits>
template <class L> void flags(const char *name) {
    std::printf("%s: %d %d %d %d %d %d %d %d", name, (int)L::is_specialized, L::digits, L::digits10,
        L::max_digits10, (int)L::is_signed, (int)L::is_integer, (int)L::is_exact, L::radix);
    std::printf(" | %d %d %d %d", L::min_exponent, L::min_exponent10, L::max_exponent, L::max_exponent10);
    std::printf(" | %d %d %d %d %d", (int)L::has_infinity, (int)L::has_quiet_NaN, (int)L::has_signaling_NaN,
        (int)L::has_denorm, (int)L::has_denorm_loss);
    std::printf(" | %d %d %d %d %d %d\n", (int)L::is_iec559, (int)L::is_bounded, (int)L::is_modulo, (int)L::traps,
        (int)L::tinyness_before, (int)L::round_style);
}
template <class L> void ints(const char *name) {
    flags<L>(name);
    std::printf("  %lld %llu %lld %lld %lld %lld %lld %lld %lld\n", (long long)L::min(), (unsigned long long)L::max(),
        (long long)L::lowest(), (long long)L::epsilon(), (long long)L::round_error(), (long long)L::infinity(),
        (long long)L::quiet_NaN(), (long long)L::signaling_NaN(), (long long)L::denorm_min());
}
template <class T, class L> void floats(const char *name) {
    flags<L>(name);
    std::printf("  %a %a %a\n", (double)L::min(), (double)L::max(), (double)L::lowest());
    std::printf("  %a %a %a %a\n", (double)L::epsilon(), (double)L::round_error(), (double)L::infinity(),
        (double)L::denorm_min());
    T q = L::quiet_NaN();
    T s = L::signaling_NaN();
    std::printf("  %d %d %d\n", (int)(q != q), (int)(s != s), (int)(L::infinity() > L::max()));
}
int main() {
    ints<std::numeric_limits<bool> >("bool");
    ints<std::numeric_limits<char> >("char");
    ints<std::numeric_limits<signed char> >("signed char");
    ints<std::numeric_limits<unsigned char> >("unsigned char");
    ints<std::numeric_limits<wchar_t> >("wchar_t");
    ints<std::numeric_limits<char16_t> >("char16_t");
    ints<std::numeric_limits<char32_t> >("char32_t");
    ints<std::numeric_limits<short> >("short");
    ints<std::numeric_limits<unsigned short> >("unsigned short");
    ints<std::numeric_limits<int> >("int");
    ints<std::numeric_limits<unsigned int> >("unsigned int");
    ints<std::numeric_limits<long> >("long");
    ints<std::numeric_limits<unsigned long> >("unsigned long");
    ints<std::numeric_limits<long long> >("long long");
    ints<std::numeric_limits<unsigned long long> >("unsigned long long");
    floats<float, std::numeric_limits<float> >("float");
    floats<double, std::numeric_limits<double> >("double");
    floats<long double, std::numeric_limits<long double> >("long double");
    ints<std::numeric_limits<const int> >("const int");
    ints<std::numeric_limits<volatile unsigned char> >("volatile unsigned char");
    floats<const volatile double, std::numeric_limits<const volatile double> >("const volatile double");
    std::printf("%f %f\n", std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::signaling_NaN());
    std::printf("%f %f\n", (double)std::numeric_limits<float>::quiet_NaN(), (double)std::numeric_limits<long double>::quiet_NaN());
    std::printf("%d %d %d %d %d\n", (int)std::round_indeterminate, (int)std::round_toward_zero, (int)std::round_to_nearest,
        (int)std::round_toward_infinity, (int)std::round_toward_neg_infinity);
    std::printf("%d %d %d\n", (int)std::denorm_indeterminate, (int)std::denorm_absent, (int)std::denorm_present);
    std::printf("%d\n", (int)std::numeric_limits<std::FILE *>::is_specialized);
    return 0;
}
