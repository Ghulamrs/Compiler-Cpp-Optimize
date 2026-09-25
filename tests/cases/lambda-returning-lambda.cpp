// A21: a lambda returning a lambda - the closure built while the outer's
// body is read for its return type is the one the real reading hands back;
// three deep; a lambda in a template's body once per instantiation; and a
// nested closure copying the enclosing closure's captured this.
extern "C" int printf(const char *, ...);
struct K {
    int k;
    int run() { auto outer = [this](int a) { return [this, a](int b) { return k + a + b; }; }; return outer(10)(100); }
};
int later();
int main() {

    auto l1 = [](int a) { return [a](int b) { return [a, b](int c) { return a * 100 + b * 10 + c; }; }; };
    auto twice = [](int x) { return [x]() { return x * 2; }; };
    auto t1 = twice(4); auto t2 = twice(5);
    K kk; kk.k = 1000;
    printf("%d %d %d %d %d\n", l1(1)(2)(3), t1(), t2(), kk.run(), later());
    return 0;
}
template <class T> T tw(T v) { auto l = [v]() { return v * 2; }; return l(); }
struct Twice { int go() { return tw(3) * 1000 + (int)(tw(2.5) * 10); } };
int later() { Twice t; return t.go(); }
