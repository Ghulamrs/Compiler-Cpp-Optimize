#include <cstdio>
enum class E : short { a = 1, b = 3 }; enum class F { x, y };
int main() { E e = E::b; int i = static_cast<int>(e); E f = static_cast<E>(2); E g = E(1); bool lt = E::a < E::b; bool eq = (e == E::b);
switch (e) { case E::a: std::printf("a"); break; case E::b: std::printf("b"); break; } F ff{}; std::printf(" %d %d %d %d %d %d %d\n", i, (int)f, (int)g, (int)lt, (int)eq, (int)ff, (int)sizeof(E)); return 0; }
