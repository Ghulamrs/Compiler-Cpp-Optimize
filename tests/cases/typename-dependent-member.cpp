// A27: 'typename' before a template parameter's member type, where a type
// is read; X::value in an expression needs none.
extern "C" int printf(const char *, ...);
struct T { typedef int type; static const int value = 5; };
template <class X> struct U { typename X::type v; int w() { return X::value + (int)sizeof(typename X::type); } };
template <class X> typename X::type twice(X) { return 2 * X::value; }
int main() { U<T> u; u.v = 3; printf("%d %d %d\n", u.v, u.w(), twice(T())); return 0; }
