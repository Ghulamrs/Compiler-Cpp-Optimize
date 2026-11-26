// [temp.class.spec.match] with [temp.deduct.type]: a pattern written without cv does not match a cv-qualified
// argument - `K<T *>` leaves `int *const` to `K<const T>`.
#include <cstdio>
template <class T> struct K { static const char *n() { return "T"; } };
template <class T> struct K<T *> { static const char *n() { return "T*"; } };
template <class T> struct K<const T *> { static const char *n() { return "const T*"; } };
template <class T> struct K<const T> { static const char *n() { return "const T"; } };
template <class T, int N> struct K<T[N]> { static const char *n() { return "T[N]"; } };
template <class T> struct V { static const char *n() { return "T"; } };
template <class T> struct V<volatile T> { static const char *n() { return "volatile T"; } };
template <class T> struct V<T &> { static const char *n() { return "T&"; } };
int main() {
    std::printf("%s|%s|%s|%s\n", K<const int *const>::n(), K<int *const>::n(), K<const int *>::n(), K<int *>::n());
    std::printf("%s|%s|%s\n", K<int[2]>::n(), K<const char>::n(), K<int>::n());
    std::printf("%s|%s|%s\n", V<volatile int *>::n(), V<int *volatile>::n(), V<const int &>::n());
    return 0;
}
