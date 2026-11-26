void f(int); void f(double); int main() { void (*p)(char *) = &f; return 0; }
