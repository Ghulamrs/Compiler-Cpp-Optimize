#include <cstdio>
struct S { int v = 5; int f() { auto outer = [this](int x) { auto inner = [this, x]() { return v + x; }; return inner(); }; return outer(2); }
  int g() const { return [this] { return v; }(); } };
int main() { S s; std::printf("%d %d\n", s.f(), s.g()); return 0; }
