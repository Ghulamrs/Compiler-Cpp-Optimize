// Elements the list leaves out need a default constructor, and N has none.
struct N { int v; N(int a) : v(a) {} };
int main() { N a[3] = { 1 }; return a[0].v; }
