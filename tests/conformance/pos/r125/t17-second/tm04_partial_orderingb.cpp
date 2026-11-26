// [temp.func.order]: more specialised function templates win: T* over T, const T& vs T&, T(&)[N] over T*, Box<T> over T, two-pack vs one; with explicit args; with default args.
#include <cstdio>
template <class T> struct Box { T v; };
template <class T> const char *f(T) { return "T"; }
template <class T> const char *f(T *) { return "T*"; }
template <class T> const char *f(const T *) { return "const T*"; }
template <class T> const char *f(Box<T>) { return "Box<T>"; }
template <class T, int N> const char *f(T (&)[N]) { return "T(&)[N]"; }
template <class T> const char *g(T, T) { return "T,T"; }
template <class T, class U> const char *g(T, U) { return "T,U"; }
template <class T> const char *h(T) { return "T"; }
template <class T> const char *h(T *) { return "T*"; }
template <class T> const char *h(T **) { return "T**"; }
template <class T = int> const char *d() { return "default"; }
template <class T> const char *k(const T &) { return "const T&"; }
template <class T> const char *k(T &&) { return "T&&"; }
int main() {
    int i = 0; int *p = &i; const int *cp = &i; Box<int> b = {1}; int arr[2]; int **pp = &p;
    std::printf("%s %s %s %s\n", f(i), f(p), f(cp), f(b));
    std::printf("%s %s %s\n", g(1, 2), g(1, 2.0), g<int>(1, 2.0));
    std::printf("%s %s %s\n", h(i), h(p), h(pp));
    std::printf("%s %s\n", d(), d<double>());
    const int ci = 0; std::printf("%s %s %s\n", k(i), k(ci), k(1));
    return 0;
}
