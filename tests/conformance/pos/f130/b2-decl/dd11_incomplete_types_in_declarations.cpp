#include <cstdio>
struct Fwd; Fwd *make(); int use(Fwd *); extern Fwd *gp; extern int incomplete_arr[]; struct Fwd { int v; }; Fwd f1{1}; Fwd *gp = &f1; int incomplete_arr[2] = {5, 6};
Fwd *make() { return &f1; } int use(Fwd *p) { return p->v; } struct Node { Node *next; int v; }; struct Self { static Self instance; int v; }; Self Self::instance{9};
int main() { Node n{nullptr, 3}; std::printf("%d %d %d %d %d\n", use(make()), gp->v, incomplete_arr[1], n.v, Self::instance.v); return 0; }
