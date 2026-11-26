struct S { int a; int b; constexpr S(int x) : a(x) {} }; int main() { constexpr S s(1); return s.a; }
