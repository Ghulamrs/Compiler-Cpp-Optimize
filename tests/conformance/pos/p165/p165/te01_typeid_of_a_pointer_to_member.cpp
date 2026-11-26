// [expr.typeid]: a pointer to member has a type_info, told apart by its class and by its member's type, and
// std::function reads it for target_type and target.
#include <cstdio>
#include <functional>
#include <typeinfo>
struct S {
    int m;
    int n;
    long k;
    int get(int v) const { return m + v; }
    int set(int v) { m = v; return v; }
};
struct T { int m; };
typedef int S::*DataOfS;
typedef int (S::*ConstFnOfS)(int) const;
typedef int (S::*FnOfS)(int);
int main() {
    std::printf("%d\n", typeid(int S::*) == typeid(DataOfS) ? 1 : 0);
    std::printf("%d\n", typeid(int S::*) == typeid(int T::*) ? 1 : 0);
    std::printf("%d\n", typeid(int S::*) == typeid(long S::*) ? 1 : 0);
    std::printf("%d\n", typeid(FnOfS) == typeid(ConstFnOfS) ? 1 : 0);
    std::printf("%d\n", typeid(ConstFnOfS) == typeid(int (S::*)(int) const) ? 1 : 0);
    DataOfS held = &S::n;
    std::printf("%d\n", typeid(held) == typeid(int S::*) ? 1 : 0);
    std::printf("%d\n", typeid(DataOfS *) == typeid(int S::**) ? 1 : 0);
    std::printf("%d\n", typeid(DataOfS *) == typeid(FnOfS *) ? 1 : 0);
    std::function<int(const S &)> byData = &S::n;
    S s;
    s.m = 1;
    s.n = 2;
    s.k = 3;
    std::printf("%d\n", byData(s));
    std::printf("%d\n", byData.target_type() == typeid(int S::*) ? 1 : 0);
    DataOfS *inside = byData.target<DataOfS>();
    std::printf("%d\n", inside != 0 && *inside == &S::n ? 1 : 0);
    std::printf("%d\n", byData.target<ConstFnOfS>() == 0 ? 1 : 0);
    std::function<int(const S &, int)> byFn = &S::get;
    std::printf("%d\n", byFn(s, 4));
    std::printf("%d\n", byFn.target_type() == typeid(ConstFnOfS) ? 1 : 0);
    return 0;
}
