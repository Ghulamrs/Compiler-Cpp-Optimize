struct N { N() {} }; struct S { static constexpr N n = N(); }; int main() { return 0; }
