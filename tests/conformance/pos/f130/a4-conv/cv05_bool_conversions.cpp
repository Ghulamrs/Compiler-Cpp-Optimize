#include <cstdio>
struct S { explicit operator bool() const { return true; } }; struct T { operator int() const { return 2; } };
int main() { S s; T t; int *p = nullptr; double d = 0.5; if (s) std::printf("s "); bool b1 = !s; bool b2 = p; bool b3 = d; bool b4 = t; bool b5 = s && true; bool b6 = static_cast<bool>(s);
std::printf("%d %d %d %d %d %d\n", (int)b1, (int)b2, (int)b3, (int)b4, (int)b5, (int)b6); return 0; }
