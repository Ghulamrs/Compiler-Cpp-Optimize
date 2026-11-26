template <class T> T &&fwd(T &t) { return static_cast<T &&>(t); }
int add(int a, int b) { return a + b; }
template <class F, class... A> auto call(F f, A... a) -> decltype(f(a...)) { return f(a...); }
int main() { return call(add, 1, 2) == 3 ? 0 : 1; }
