// [temp.res]/9-10, [temp.dep.candidate]: non-dependent names bound at definition; dependent calls find later declarations via ADL only; the famous g(T) example; template specialisation visible before instantiation.
#include <cstdio>
void g(double) { std::printf("g(double) "); }
void h(int) { std::printf("h(int)-early "); }
template <class T> void call(T t) { g(t); h(t); }
void g(int) { std::printf("g(int) "); }
void h(double) { std::printf("h(double)-late "); }
namespace NS { struct X {}; }
template <class T> void dep(T t) { found(t); }
namespace NS { void found(X) { std::printf("adl-found "); } }
template <class T> const char *spec() { return "primary"; }
template <> const char *spec<int>() { return "spec-int"; }
int main() {
    call(1); std::printf("\n");
    dep(NS::X()); std::printf("\n");
    std::printf("%s %s\n", spec<int>(), spec<char>());
    return 0;
}
