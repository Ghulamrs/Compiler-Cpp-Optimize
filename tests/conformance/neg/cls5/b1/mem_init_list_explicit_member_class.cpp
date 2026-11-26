struct E { explicit E(int) {} };
struct S { E es[1]; S() : es{1} {} };
int main() { S s; return 0; }
