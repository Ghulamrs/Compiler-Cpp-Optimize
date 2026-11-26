#include <initializer_list>
template <class T> void f(std::initializer_list<T>); int main() { f({1, 2.0}); return 0; }
