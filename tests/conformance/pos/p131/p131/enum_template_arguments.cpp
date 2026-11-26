#include <cstdio>
enum E { e1 = 1 }; enum class F : short { f1 = 2 };
template <class T> struct Box { static int id() { return (int)sizeof(T); } };
template <> struct Box<int> { static int id() { return 40; } };
template <class T> int z(T *) { return 1; } template <> int z(int *) { return 2; }
int main() { E *pe = 0; std::printf("%d %d %d %d %d\n", Box<E>::id(), Box<int>::id(), Box<F>::id(), z(pe), z((int*)0)); return 0; }
