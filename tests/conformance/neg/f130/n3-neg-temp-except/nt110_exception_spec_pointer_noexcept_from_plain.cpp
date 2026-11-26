void f(); int main() { void (*p)() noexcept = &f; return 0; }
