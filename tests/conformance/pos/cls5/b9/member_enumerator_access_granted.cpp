// A class's private and protected enumerators named from its members, a nested class, a friend, a lambda
// in a member function, a derived class, and its public ones from anywhere.
#include <cstdio>

class S {
    enum Mode { off, on, both };
    enum { size = 8 };
    friend int peek();
public:
    enum Colour { red = 10, green };
    int get() const { return S::both + S::size; }
    struct Inner {
        int f() const { return S::on; }
    };
    int viaLambda() const {
        auto l = [](int v) { return v + S::size; };
        return l(1);
    }
};

int peek() { return S::size * 2; }

struct B {
protected:
    enum { limit = 4 };
};

struct D : B {
    int h() const { return B::limit + D::limit; }
};

int main() {
    S s;
    S::Inner in;
    D d;
    std::printf("%d %d %d %d %d %d %d\n", s.get(), in.f(), s.viaLambda(), peek(), d.h(), S::red, S::green);
    return 0;
}
