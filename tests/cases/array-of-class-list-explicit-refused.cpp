// An element is copy-initialised from its initialiser, which may not pick an explicit constructor.
struct E { int v; explicit E(int a) : v(a) {} };
int main() { E a[1] = { 1 }; return a[0].v; }
