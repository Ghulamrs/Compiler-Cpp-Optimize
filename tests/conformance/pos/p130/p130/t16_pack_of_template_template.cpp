template <class T> struct A {}; template <class T> struct B {};
template <template <class> class... Ts> struct L { static const int n = sizeof...(Ts); };
int main() { return L<A, B>::n == 2 ? 0 : 1; }
