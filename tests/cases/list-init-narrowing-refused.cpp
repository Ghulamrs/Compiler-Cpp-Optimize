// [dcl.init.list]/7: an element of a braced list may not narrow, and that
// holds when the braces reach a constructor - 2.5 cannot become the `int` the
// parameter asks for.
struct P { int a, b; P(int x, int y) : a(x), b(y) {} };
int main() {
    P p{1, 2.5};
    return p.a;
}
