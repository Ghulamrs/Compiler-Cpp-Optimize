// [dcl.init.aggr]/2: a narrowing element conversion in a braced list.
int main() { double x = 1.5; int *p = new int[2]{x, 2}; delete[] p; return 0; }