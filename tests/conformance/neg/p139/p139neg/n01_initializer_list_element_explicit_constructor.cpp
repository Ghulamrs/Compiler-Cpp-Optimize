// [dcl.init.list]/5: each element is copy-initialised, which an explicit constructor cannot do.
#include <initializer_list>
struct E { explicit E(int) {} };
int count(std::initializer_list<E> l) { return (int)l.size(); }
int main() { return count({1, 2}); }