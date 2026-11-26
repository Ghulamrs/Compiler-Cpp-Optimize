#include <cstdio>
template <class T> class Box { T v; template <class U> friend class Box; friend bool operator==(const Box &a, const Box &b) { return a.v == b.v; } template <class U> friend U unbox(const Box<U> &); public: Box(T v) : v(v) {} template <class U> Box(const Box<U> &o) : v((T)o.v) {} static int made; }; template <class T> int Box<T>::made = 0; template <class U> U unbox(const Box<U> &b) { return b.v; }
int main() { Box<int> a(1), b(1); Box<double> d(2.5); Box<int> c(d); std::printf("%d %d %g %d\n", (int)(a == b), unbox(c), unbox(d), Box<int>::made); return 0; }
