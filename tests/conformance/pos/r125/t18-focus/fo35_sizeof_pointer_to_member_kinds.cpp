// Microsoft ABI sizes of pointers to members for single-, multiple- and virtual-inheritance classes, and of a pointer to member function of an incomplete class is not asked (cl: 8/16/24).
#include <cstdio>
struct S { int a; virtual ~S() {} };
struct V : virtual S { int b; };
int main() { std::printf("%d %d %d %d\n", (int)sizeof(int S::*), (int)sizeof(int (S::*)()), (int)sizeof(int V::*), (int)sizeof(int (V::*)())); return 0; }
