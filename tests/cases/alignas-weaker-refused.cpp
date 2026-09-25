// [dcl.align]/5: an alignment weaker than the type's own is ill-formed.
struct S { alignas(1) int x; };
int main() { S s; s.x = 1; return s.x; }
