#include <cstdio>
template <class T> int sz(T) { return (int)sizeof(T); } template <class T> struct W { T t; }; enum { anon_e = 3 }; struct { int a; int b; } anon_s;
int main() { struct Local { int x, y, z; }; Local l{1, 2, 3}; W<Local> w{l}; enum LE { le = 1 }; std::printf("%d %d %d %d %d\n", sz(l), w.t.z, sz(anon_s), sz(le), sz(anon_e)); return 0; }
