#include <cstdio>
class B { protected: int p = 4; static int sp; int f() { return 1; } }; int B::sp = 6; class D : public B { public: int get() { return p + f() + sp; } int other(D &d) { return d.p; } static int st(D &d) { return d.p + sp; } };
int main() { D d; std::printf("%d %d %d\n", d.get(), d.other(d), D::st(d)); return 0; }
