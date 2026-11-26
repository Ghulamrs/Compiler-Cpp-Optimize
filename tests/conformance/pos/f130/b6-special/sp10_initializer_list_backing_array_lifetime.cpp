#include <cstdio>
#include <initializer_list>
struct T { int id; T(int i) : id(i) { std::printf("+%d ", id); } T(const T &o) : id(o.id + 10) { std::printf("c%d ", id); } ~T() { std::printf("-%d ", id); } };
int count(std::initializer_list<T> l) { std::printf("in "); return (int)l.size(); }
int main() { int n = count({T(1), T(2)}); std::printf("n%d ", n); { std::initializer_list<T> il = {T(3)}; std::printf("il%d ", (int)il.size()); } std::printf("\n"); return 0; }
