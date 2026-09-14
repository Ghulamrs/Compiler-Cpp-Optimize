// **A pointer to a virtual member function dispatches.** Itanium keeps
// 1 + the slot's offset in the code word and the call tests the low bit;
// Microsoft's single code pointer names a vcall thunk, `??_9S@@$BA@AA`,
// that reads the vftable. Refused by name until 2026-09-14 (review A4).
extern "C" int printf(const char *, ...);
struct S {
    int v;
    S(int x) : v(x) {}
    virtual int get() { return v; }
    virtual int add(int a, int b) { return v + a + b; }
    int plain(int k) { return v * k; }
    virtual ~S() {}
};
struct T : S {
    T(int x) : S(x) {}
    int get() { return v * 10; }
    int add(int a, int b) { return v * 100 + a + b; }
};
typedef int (S::*Getter)();
typedef int (S::*Adder)(int, int);
typedef int (S::*Scaler)(int);
int call(S &s, Getter g) { return (s.*g)(); }
int callp(S *s, Adder a, int x, int y) { return (s->*a)(x, y); }
int main() {
    S s(3);
    T t(4);
    Getter g = &S::get;
    Adder a = &S::add;
    Scaler p = &S::plain;
    S *ps = &t;
    printf("%d %d %d %d\n", (s.*g)(), (t.*g)(), (ps->*g)(), (s.*p)(5));
    printf("%d %d %d %d\n", (s.*a)(1, 2), (t.*a)(1, 2), call(t, g), callp(&s, a, 7, 8));
    printf("%d %d %d\n", call(s, &S::get), callp(&t, &S::add, 0, 1), (s.*p)(2));
    return 0;
}
