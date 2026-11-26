// [over.match.best]: for a B temporary, A(const B &) and B::operator A() const are no better than each other.
struct B;
struct A {
    A(const B &) {}
};
struct B {
    operator A() const { return A(*this); }
};
static int take(A) { return 1; }
int main() {
    return take(B());
}
