// A trailing return type - [dcl.fct]/2: `auto f(params) -> T` says what f
// returns after the parameters, which are in scope for it, so `decltype(a)` and
// a member's type name the parameters; on a member, the class's scope is in
// force too, so a nested type needs no qualifier. Refused by name until now.
// A free function's return type is not in its name on either ABI: _Z3addii; a
// template's is, as for any function template: _Z7doubledIiET_S0_.
extern "C" int printf(const char *, ...);

auto add(int a, int b) -> int { return a + b; }
auto twice(double x) -> decltype(x * 2) { return x * 2; }
auto widen(int a) -> long long;
auto widen(int a) -> long long { return a * 1000000000LL; }

template <class T> auto doubled(T a) -> T { return a + a; }

int negate(int v) { return -v; }
auto pick(int k) -> int (*)(int) { return k > 0 ? negate : 0; }

struct S {
    int v;
    struct In { int z; };
    S(int x);
    auto get() const -> int;
    auto scaled(int k) -> decltype(k * 2L);
    auto make() -> In;
    auto inl(int a) const -> int { return a + v; }
};

S::S(int x) : v(x) {}
auto S::get() const -> int { return v; }
auto S::scaled(int k) -> decltype(k * 2L) { return v * k; }
auto S::make() -> In { In i; i.z = v + 1; return i; }

auto main() -> int {
    auto later(int) -> int;
    printf("%d %.1f %lld\n", add(2, 3), twice(1.25), widen(3));
    printf("%d %.1f %d\n", doubled(4), doubled(1.5), later(6));
    printf("%d\n", pick(1)(7));
    S s(4);
    printf("%d %ld %d %d\n", s.get(), s.scaled(5), s.make().z, s.inl(10));
    return 0;
}

auto later(int a) -> int { return a * 7; }
