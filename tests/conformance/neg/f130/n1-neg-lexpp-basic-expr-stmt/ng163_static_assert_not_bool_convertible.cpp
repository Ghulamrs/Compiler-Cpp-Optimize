struct S {}; int main() { static_assert(S(), "s"); return 0; }
