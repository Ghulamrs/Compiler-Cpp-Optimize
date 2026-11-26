#include <type_traits>
template <class T, class U = int> struct D { T r; };
template <class To, class R, class P> typename std::enable_if<sizeof(To) != 0, To>::type dc(const D<R, P> &d) { To t; t.r = d.r; return t; }
int main() { D<long, char> x; x.r = 5; return (int)dc<D<long long> >(x).r - 5; }
