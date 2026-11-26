// [temp.deduct.call]/1: each element deduces T, and int and double disagree.
#include <initializer_list>
template <class T> int il(std::initializer_list<T> l) { return (int)l.size(); }
int main() { return il({1, 2.0}); }