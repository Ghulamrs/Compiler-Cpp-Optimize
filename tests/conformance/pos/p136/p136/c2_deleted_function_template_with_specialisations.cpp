// R10: [dcl.fct.def.delete] on a function template, with explicit specializations it keeps.
#include <cstdio>
template <class T> void gen(T) = delete;
template <> void gen(char) { std::printf("char "); }
template <> void gen<double>(double d) { std::printf("double %g ", d); }
template <class T> int only(T) = delete;
template <> int only(int i) { return i * 2; }
int main() { gen('c'); gen(1.5); std::printf("%d\n", only(21)); return 0; }