// [except.throw], [except.handle]: an exception of any complete type, so a pointer to a function, a pointer to a pointer
// and a pointer to a member are thrown and caught like any other, each by its own type and with its value.
#include <cstdio>
struct S {
    int m;
    int n;
    int get() const { return n; }
    int twice(int v) { return 2 * v + n; }
};
int seven() { return 7; }
int main() {
    typedef int (*Fn)();
    typedef int S::*Data;
    typedef int (S::*Method)(int);
    typedef int (S::*ConstMethod)() const;
    S s;
    s.m = 1;
    s.n = 2;
    int x = 3;
    int *p = &x;
    try {
        throw static_cast<Fn>(seven);
    } catch (int) {
        std::printf("int\n");
    } catch (Fn f) {
        std::printf("function %d\n", f());
    }
    try {
        throw &p;
    } catch (int *) {
        std::printf("int*\n");
    } catch (int **q) {
        std::printf("pointer to pointer %d\n", **q);
    }
    try {
        throw &p;
    } catch (void *v) {
        std::printf("void* %d\n", v == static_cast<void *>(&p) ? 1 : 0);
    }
    try {
        int *const *c = &p;
        throw c;
    } catch (int *const *q) {
        std::printf("int*const* %d\n", **q);
    }
    try {
        throw &S::n;
    } catch (long S::*) {
        std::printf("long member\n");
    } catch (Data d) {
        std::printf("data %d\n", s.*d);
    }
    try {
        throw &S::twice;
    } catch (ConstMethod) {
        std::printf("const method\n");
    } catch (Method f) {
        std::printf("method %d\n", (s.*f)(4));
    }
    try {
        throw &S::get;
    } catch (ConstMethod &f) {
        std::printf("const method %d\n", (s.*f)());
    }
    return 0;
}
