// [class.access]/1: a protected enumerator reached through a derived class from a function that is no member.
struct B {
protected:
    enum { limit = 4 };
};
struct D : B {};
int main() {
    return D::limit;
}
