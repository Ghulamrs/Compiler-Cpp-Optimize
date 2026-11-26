#include <cstdio>
class S { int v = 5; public: int via() const { struct L { int get(const S &s) { return s.v; } }; return L().get(*this); } };
int main() { S s; std::printf("%d\n", s.via()); return 0; }
