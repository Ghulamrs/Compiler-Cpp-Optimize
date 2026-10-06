// [dcl.spec.auto]/3: an 'auto' variable is in scope in its own initialiser
// and has no type there yet, so naming it is refused - clang refuses it too.
int main() {
    auto a = sizeof(a);
    return (int)a;
}
