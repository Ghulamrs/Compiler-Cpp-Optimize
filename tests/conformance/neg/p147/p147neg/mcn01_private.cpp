// [class.access]/1: a private member class template is not named from outside its class.
class C { template <class T> struct P { T t; }; public: int f() const { P<int> p; p.t = 1; return p.t; } };
int main() { C::P<int> x; x.t = 2; return C().f() + x.t; }
