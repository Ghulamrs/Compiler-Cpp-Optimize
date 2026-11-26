#include <cstdio>

// [class.protected]: a protected base destructor is the derived class's to call - the implicit destructor of a
// class deriving from one, as a program's facet derives from a standard one, calls it.
static int destroyed = 0;
class Base {
public:
    virtual int value() const { return 1; }
protected:
    ~Base() { destroyed++; }
};
class Derived : public Base {
public:
    int value() const { return 2; }
};
struct Holder {
    Derived d;
};

int main() {
    {
        Derived a;
        Holder h;
        std::printf("%d %d\n", a.value(), h.d.value());
    }
    Derived *p = new Derived;
    delete p;
    std::printf("%d\n", destroyed);
    return 0;
}
