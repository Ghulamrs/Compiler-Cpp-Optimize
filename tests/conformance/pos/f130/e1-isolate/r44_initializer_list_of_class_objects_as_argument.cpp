#include <cstdio>
#include <initializer_list>
struct T { int id; T(int i) : id(i) {} T(const T &o) : id(o.id + 10) {} ~T() {} };
int count(std::initializer_list<T> l) { return (int)l.size(); }
int main() { std::printf("%d\n", count({T(1), T(2)})); return 0; }
