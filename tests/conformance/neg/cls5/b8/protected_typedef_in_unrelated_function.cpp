// [class.access]/1: a protected typedef reached through a derived class from a function that is no member of either.
struct B {
protected:
    typedef int T;
};
struct D : B {};
static int g() {
    D::T x = 2;
    return x;
}
int main() {
    return g();
}
