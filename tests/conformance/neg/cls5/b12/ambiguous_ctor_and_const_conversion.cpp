// [over.match.best]: A(const B &) and B::operator A() const bind the same reference to b, so neither is better.
struct B;
struct A {
    A(const B &) {}
};
struct B {
    operator A() const { return A(*this); }
};
static int take(A) { return 1; }
int main() {
    B b;
    return take(b);
}
