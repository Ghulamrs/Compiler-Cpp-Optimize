void f(volatile int *) {} int main() { const int c = 1; f(&c); return 0; }
