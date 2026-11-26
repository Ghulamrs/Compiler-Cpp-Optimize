void f(); int main() { void (*p)() throw() = f; return 0; }
