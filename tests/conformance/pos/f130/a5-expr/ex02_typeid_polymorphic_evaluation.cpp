#include <cstdio>
#include <typeinfo>
struct B { virtual ~B() {} }; struct D : B {}; struct N {};
B *make() { std::printf("made "); return new D; }
int main() { B *b = new D; B &r = *b; std::printf("%d %d %d ", (int)(typeid(r) == typeid(D)), (int)(typeid(*b) == typeid(D)), (int)(typeid(B) == typeid(D))); N *n = nullptr; std::printf("%d ", (int)(typeid(*n) == typeid(N))); const std::type_info &t = typeid(*make()); std::printf("%d %d\n", (int)(t == typeid(D)), (int)(typeid(int) == typeid(const int))); delete b; return 0; }
