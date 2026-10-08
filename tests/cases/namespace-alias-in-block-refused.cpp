// A namespace alias inside a block, refused by name. [namespace.alias] allows
// it, and clang compiles this; the alias here is a rewrite of the tokens to the
// end of the enclosing *namespace*, and a block's end is not one of those.
// Written at namespace scope the same alias works - namespace-alias.cpp.
namespace N {
    int f(void) { return 3; }
}

int main(void) {
    namespace A = N;
    return A::f();
}
