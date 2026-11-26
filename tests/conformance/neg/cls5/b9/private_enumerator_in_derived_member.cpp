// [class.access]/1: a derived class's member has no access to its base's private enumerator.
class B {
    enum E { k = 5 };
};
struct D : B {
    int f() const { return B::k; }
};
int main() {
    D d;
    return d.f();
}
