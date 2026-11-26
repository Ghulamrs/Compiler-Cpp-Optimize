// [over.best.ics]/6: A(B) takes b by an identity conversion, as B::operator A() binds its object - ambiguous.
struct B;
struct A {
    A(B);
};
struct B {
    operator A();
};
static int take(A) { return 1; }
int main() {
    B b;
    return take(b);
}
