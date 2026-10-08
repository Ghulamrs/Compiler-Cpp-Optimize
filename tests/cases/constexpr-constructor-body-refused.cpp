// [dcl.constexpr]/4: a C++11 constexpr constructor's body is empty, its work done in the
// initialiser list; a statement in it is C++14. clang -std=c++11 -pedantic-errors: "use of
// this statement in a constexpr constructor is a C++14 extension" (4:47).
struct P { int x; constexpr P(int a) : x(0) { x = a; } };
int main() { P p(1); return p.x - 1; }
