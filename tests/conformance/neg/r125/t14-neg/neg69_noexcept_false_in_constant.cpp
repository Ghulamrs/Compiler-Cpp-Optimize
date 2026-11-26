void f();
int main() { static_assert(noexcept(f()), "f is not noexcept"); return 0; }
