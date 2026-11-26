void f(); int main() { void (*p)() = f; p++; return 0; }
