// [over.match.best]/1: non-template preferred over template on a tie; more specialised template wins; explicit template args; overload between T and T*; between T& and const T&.
#include <cstdio>
template <class T> const char *f(T) { return "T"; }
const char *f(int) { return "int"; }
template <class T> const char *g(T) { return "T"; }
template <class T> const char *g(T *) { return "T*"; }
template <class T> const char *h(T &) { return "T&"; }
template <class T> const char *h(const T &) { return "const T&"; }
template <class T> const char *k(T) { return "T"; }
template <> const char *k<int>(int) { return "spec<int>"; }
template <class T, class U> const char *p(T, U) { return "T,U"; }
template <class T> const char *p(T, T) { return "T,T"; }
template <class T> const char *q(T) { return "T"; }
template <class T> const char *q(const T *) { return "const T*"; }
int main() {
    int i = 1; double d = 2; int *ip = &i; const int ci = 3; const int *cip = &ci;
    std::printf("%s %s %s\n", f(i), f(d), f<int>(i));
    std::printf("%s %s %s\n", g(i), g(ip), g<int *>(ip));
    std::printf("%s %s %s\n", h(i), h(ci), h(ip));
    std::printf("%s %s %s\n", k(i), k(d), k<double>(1));
    std::printf("%s %s %s\n", p(1, 2), p(1, 2.0), p(i, i));
    std::printf("%s %s %s\n", q(ip), q(cip), q(i));
    return 0;
}
