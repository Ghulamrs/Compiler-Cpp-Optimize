template <class Sig> struct fn;
template <class R, class... A> struct fn<R(A...)> { typedef R result; static const int n = sizeof...(A); };
int main() { return fn<int(char, long)>::n == 2 ? 0 : 1; }
