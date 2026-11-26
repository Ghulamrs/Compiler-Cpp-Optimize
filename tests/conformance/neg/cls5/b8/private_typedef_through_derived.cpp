// [class.access]/1: a base's private typedef named through a derived class is still the base's private member.
class B {
    typedef int T;
};
struct D : B {};
int main() {
    D::T x = 0;
    return x;
}
