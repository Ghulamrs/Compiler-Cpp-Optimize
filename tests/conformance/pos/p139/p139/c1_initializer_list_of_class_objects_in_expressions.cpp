// R21: [dcl.init.list]/5-6 - an initializer_list of class objects made inside an expression: each element
// built in place, the array destroyed at the end of the full expression, last element first.
#include <cstdio>
#include <initializer_list>
struct T { int id; T(int i) : id(i) { std::printf("+%d ", id); } T(const T &o) : id(o.id + 10) { std::printf("c%d ", id); } ~T() { std::printf("-%d ", id); } };
int count(std::initializer_list<T> l) { std::printf("in "); return (int)l.size(); }
int sum(std::initializer_list<T> l) { int s = 0; for (const T &t : l) s += t.id; return s; }
int main() {
    int a = count({T(1), T(2)});
    std::printf("a%d ", a);
    int b = count({4, 5, 6});
    std::printf("b%d ", b);
    int c = sum({T(7), 8}) + 1;
    std::printf("c%d\n", c);
    { std::initializer_list<T> il = {T(3), 9}; std::printf("il%d ", (int)il.size()); }
    std::printf("\n");
    return 0;
}