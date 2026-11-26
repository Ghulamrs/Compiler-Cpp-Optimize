struct S { int v; int f() { auto l = [=, this] { return v; }; return l(); } }; int main() { S s{1}; return s.f(); }
