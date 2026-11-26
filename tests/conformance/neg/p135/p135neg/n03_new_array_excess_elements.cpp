// [dcl.init.aggr]/6: more clauses than a constant count of elements.
int main() { int *p = new int[2]{1, 2, 3}; delete[] p; return 0; }