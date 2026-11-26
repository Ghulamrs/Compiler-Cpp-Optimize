// [lex.icon]: the type of an integer literal is the first that fits; 2147483648 is long long, 4294967295 is long long (decimal), 0xFFFFFFFF is unsigned int.
#include <cstdio>
template <class T> struct Name { static const char *get() { return "other"; } };
template <> struct Name<int> { static const char *get() { return "int"; } };
template <> struct Name<unsigned> { static const char *get() { return "unsigned"; } };
template <> struct Name<long> { static const char *get() { return "long"; } };
template <> struct Name<unsigned long> { static const char *get() { return "unsigned long"; } };
template <> struct Name<long long> { static const char *get() { return "long long"; } };
template <> struct Name<unsigned long long> { static const char *get() { return "unsigned long long"; } };
template <class T> const char *nameOf(T) { return Name<T>::get(); }
int main() {
    std::printf("%s %s %s %s\n", nameOf(2147483647), nameOf(2147483648), nameOf(0xFFFFFFFF), nameOf(4294967295));
    std::printf("%s %s %s %s\n", nameOf(1L), nameOf(1UL), nameOf(1LL), nameOf(1ULL));
    std::printf("%s %s %s\n", nameOf(0x7FFFFFFF), nameOf(0x80000000), nameOf(017777777777));
    std::printf("%d %d %d\n", (int)sizeof(2147483648), (int)sizeof(0xFFFFFFFF), (int)sizeof(1ull));
    std::printf("%lld %llu\n", -9223372036854775807LL - 1, 18446744073709551615ULL);
    return 0;
}
