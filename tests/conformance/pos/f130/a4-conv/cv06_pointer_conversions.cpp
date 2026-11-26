#include <cstdio>
struct B { int b; }; struct D : B { int d; }; struct D2 : B { int d2; }; struct M : D, D2 {};
int main() { M m; D *pd = &m; B *pb = pd; int B::*pm = &B::b; int D::*pmd = pm; const void *pv = pb; D2 *pd2 = &m; B *pb2 = pd2;
std::printf("%d %d %d %d\n", (int)(pb != pb2), (int)(pv == pb), (int)(pmd == pm), (int)(static_cast<D*>(pb) == pd)); return 0; }
