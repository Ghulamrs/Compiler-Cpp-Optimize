// [dcl.init.ref]/5: a temporary does not bind to a reference to non-const, static storage or not.
struct S {
    int v;
};
S &r = S();
int main() {
    return r.v;
}
