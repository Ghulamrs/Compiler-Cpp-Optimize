// Placement new with more than one argument would need a user-declared
// `operator new(size_t, ...)`, which the one-parameter rule refuses.
struct S { int v; };
static char buf[sizeof(S)];
int main() { S *s = new (buf, 3) S; return s->v; }
