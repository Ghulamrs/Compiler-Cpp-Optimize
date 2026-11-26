#include <cstdio>
#include <typeinfo>
template <class T> int byValue(T) { return (int)sizeof(T); }
template <class T> struct Name { static const char *get() { return "plain"; } };
template <class T> struct Name<volatile T> { static const char *get() { return "volatile"; } };
template <class T> struct Name<const volatile T> { static const char *get() { return "cv"; } };
template <class T> const char *byRef(T &) { return Name<T>::get(); }
template <class T> const char *byPtr(T *) { return Name<T>::get(); }
template <class T> const char *byVPtr(volatile T *) { return Name<T>::get(); }
template <class T> const char *byCRef(const T &) { return Name<T>::get(); }
struct P { int x; P() : x(1) {} P(const volatile P &o) : x(o.x + 10) {} };
int main() {
    volatile int v = 3; const volatile int cv = 4; int i = 5;
    std::printf("%d %s %s %s %s\n", byValue(v), byRef(v), byRef(cv), byPtr(&cv), byVPtr(&cv));
    std::printf("%d %d %d\n", (int)(typeid(volatile int) == typeid(int)), (int)(typeid(v) == typeid(int)),
                (int)(typeid(volatile int *) == typeid(int *)));
    std::printf("%s %s %s\n", byVPtr(&i), byRef(i), byCRef(cv));
    volatile P vp; P copy = vp;
    std::printf("%d\n", copy.x);
    return 0;
}
