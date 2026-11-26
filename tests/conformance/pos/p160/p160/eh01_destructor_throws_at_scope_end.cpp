// [except.ctor]: a destructor that throws at the end of its scope has ended its object; only what is still alive
// is destroyed on the way to the handler, each once - whichever way the scope was left.
#include <cstdio>
struct Quiet { int id; explicit Quiet(int i) : id(i) {} ~Quiet() { std::printf("~Q%d ", id); } };
struct Thrower {
    int id;
    explicit Thrower(int i) : id(i) {}
    ~Thrower() noexcept(false) { std::printf("~T%d ", id); throw id; }
};
struct Holder { Thrower t; Holder() : t(5) {} };
struct Derived : Thrower { Derived() : Thrower(7) {} };
static int leaves(int how) {
    for (int i = 0; i < 2; i++) {
        Thrower t(how);
        if (how == 1) continue;
        if (how == 2) break;
        return i;
    }
    return -1;
}
int main() {
    try { Quiet q(1); Thrower t(2); Quiet r(3); } catch (int e) { std::printf("caught%d\n", e); }
    try { Thrower(4); std::printf("no\n"); } catch (int e) { std::printf("caught%d\n", e); }
    try { Holder h; } catch (int e) { std::printf("caught%d\n", e); }
    try { Derived d; } catch (int e) { std::printf("caught%d\n", e); }
    for (int how = 1; how <= 3; how++) {
        try { const int r = leaves(how); std::printf("r%d ", r); } catch (int e) { std::printf("caught%d\n", e); }
    }
    return 0;
}
