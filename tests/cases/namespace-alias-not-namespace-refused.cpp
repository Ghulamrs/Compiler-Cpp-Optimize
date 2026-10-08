// `namespace A = X;` where X is not a namespace - ill-formed, [namespace.alias]/1
// wants a qualified-namespace-specifier. clang: "expected namespace name" at
// 6:15; the refusal is at the alias.
struct S { int n; };

namespace A = S;

int main(void) { return 0; }
