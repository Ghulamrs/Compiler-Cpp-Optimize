// [expr.cond]/2: one arm may be a throw-expression; the type is the other arm's.
#include <cstdio>
int pick(bool b) { return b ? 7 : throw 1; }
int main() { std::printf("%d ", pick(true)); try { pick(false); } catch (int e) { std::printf("caught %d\n", e); } return 0; }
