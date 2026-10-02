// The same rule at file scope, where the bytes are laid down by flattenInit
// rather than stored by code: a member array of an aggregate, the string
// one character too long for it.
struct Name { char text[2]; int n; };
Name g = { "ab", 1 };
int main(void) { return g.n; }
