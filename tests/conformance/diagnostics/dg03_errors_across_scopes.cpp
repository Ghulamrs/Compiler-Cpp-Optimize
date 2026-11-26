// an error in a member's signature, a whole class, a namespace member, and a good program after them
namespace N {
    int a = nope1;
    int b = 2;
}
struct S {
    undefined_type m;
    int n;
    void f(another_missing);
    int g() { return n + nope2; }
};
int after(int x) {
    S s;
    return x + N::b + s.n;
}
int main() { return after(nope3); }
