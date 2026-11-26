// A mem-initialiser names the class's own member even when a base has a member of the same name ([class.base.init]/2: lookup in the constructor's class).
#include <cstdio>
struct Base { int t; Base() : t(1) {} };
struct Derived : Base { int t; Derived() : t(2) {} };
int main() { Derived d; std::printf("%d %d\n", d.t, d.Base::t); return 0; }
