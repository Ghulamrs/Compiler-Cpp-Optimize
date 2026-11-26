#include <cstdio>
class S { int v = 5; public: int viaLambda() const { auto l = [this] { return v * 2; }; return l(); } int viaLocalClass() const { struct L { int get(const S &s) { return s.v; } }; return L().get(*this); } };
int main() { S s; std::printf("%d %d\n", s.viaLambda(), s.viaLocalClass()); return 0; }
