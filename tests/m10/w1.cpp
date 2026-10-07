// M10 W1: three nested calls, for the line table and the call stack in cdb.
// Gate: `m10.py oracle tests/m10/w1.cpp 9` stops in inner at line 9 and `k`
// shows inner, middle, outer, main with the same file:line as cl /Zi.
#include <stdio.h>

int inner(int x) {
    int y = x * 2;
    y = y + 1;
    return y;
}

int middle(int x) {
    int r = inner(x + 1);
    return r + 3;
}

int outer(int x) {
    int m = middle(x);
    int n = m * 10;
    return n;
}

int main() {
    int v = outer(4);
    printf("%d\n", v);
    return v == 140 ? 0 : 1;
}
