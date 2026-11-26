// [dcl.init.aggr]/2: each element is copy-initialised, which an explicit constructor cannot do.
struct E { explicit E(int) {} };
int main() { E *p = new E[2]{1, 2}; delete[] p; return 0; }