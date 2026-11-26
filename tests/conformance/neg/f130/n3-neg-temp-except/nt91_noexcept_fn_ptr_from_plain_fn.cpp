void f() noexcept; void g(); int main() { void (*p)() noexcept = g; return 0; }
