#include <cstdio>
class B { int secret = 2; template <class T> friend int tpeek(const B &); }; template <class T> int tpeek(const B &b) { return b.secret + (int)sizeof(T); }
int main() { B b; std::printf("%d\n", tpeek<double>(b)); return 0; }
