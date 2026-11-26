void g(int) {}
template <class... T> void f(T... t) { g(t); }
int main() { f(1); return 0; }
