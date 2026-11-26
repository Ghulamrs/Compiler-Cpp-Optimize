// [class.access]/1: a derived class's member has no access to its base's private typedef.
class B {
    typedef int T;
};
struct D : B {
    int f() {
        B::T x = 1;
        return x;
    }
};
int main() {
    D d;
    return d.f();
}
