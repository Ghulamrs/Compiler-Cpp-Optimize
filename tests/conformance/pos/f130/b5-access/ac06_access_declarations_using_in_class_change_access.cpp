#include <cstdio>
class B { public: int pub = 1; protected: int prot = 2; }; class D : private B { public: using B::pub; using B::prot; };
int main() { D d; std::printf("%d %d\n", d.pub, d.prot); return 0; }
