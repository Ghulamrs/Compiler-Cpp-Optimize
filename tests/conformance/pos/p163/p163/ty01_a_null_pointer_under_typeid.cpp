// [expr.typeid]/2: a null pointer under `*` throws bad_typeid, in every shape of the operand.
#include <cstdio>
#include <typeinfo>
struct P { virtual ~P() {} };
struct Q : P {};
P *nothing = 0;
P *none() { return 0; }
int calls = 0;
P *counted(P *p) { calls++; return p; }
bool nullThrows(P *p) {
    try {
        (void)typeid(*p);
        return false;
    } catch (const std::bad_typeid &) {
        return true;
    }
}
bool indexedThrows(P *p) {
    try {
        (void)typeid(p[0]);
        return false;
    } catch (const std::bad_cast &) {
        return false;
    } catch (const std::exception &) {
        return true;
    }
}
const std::type_info &atStart() {
    static Q q;
    return typeid(*counted(&q));
}
struct Holder {
    P *p;
    explicit Holder(P *in) : p(in) {}
    const std::type_info &named() const { return typeid(*p); }
};
int main() {
    Q q;
    std::printf("%d\n", nullThrows(nothing) ? 1 : 0);
    std::printf("%d\n", nullThrows(&q) ? 1 : 0);
    std::printf("%d\n", indexedThrows(none()) ? 1 : 0);
    std::printf("%d\n", atStart() == typeid(Q) ? 1 : 0);
    std::printf("%d\n", calls);
    std::printf("%d\n", Holder(&q).named() == typeid(Q) ? 1 : 0);
    try {
        Holder(nothing).named();
        std::printf("no\n");
    } catch (const std::bad_typeid &) {
        std::printf("caught\n");
    }
    P *held = nothing;
    std::printf("%d\n", noexcept(typeid(*held)) ? 1 : 0);
    Q &r = q;
    std::printf("%d\n", noexcept(dynamic_cast<Q &>(r)) ? 1 : 0);
    P &up = q;
    std::printf("%d\n", noexcept(dynamic_cast<Q &>(up)) ? 1 : 0);
    std::printf("%d\n", noexcept(typeid(int)) ? 1 : 0);
    return 0;
}
