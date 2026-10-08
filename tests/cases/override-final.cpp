// `override` and `final` are C++11 and are checks, not declarations -
// [class.virtual]/4 and /5, [class]/3. An override is found by its base's slot
// whether or not the word is written, so `override` only refuses a function
// that overrides nothing; `final` on a member refuses a later override and on
// a class head refuses derivation. Both are contextual: `int final = 3;` and
// `int override = 2;` are ordinary names. Every shape here is accepted by clang.
extern "C" int printf(const char *, ...);

struct B {
    virtual int f() const { return 1; }
    virtual int g(int k) { return k; }
    virtual int h() = 0;
    virtual ~B() {}
};

struct D : B {
    int f() const override { return 2; }
    int g(int k) final { return k * 2; }
    int h() override final { return 3; }
    ~D() override {}
};
struct E : D {
    // f is not final in D, so it may be overridden once more; g and h may not.
    int f() const override { return 4; }
};
struct Leaf final : B {
    int f() const override { return 5; }
    int h() override { return 6; }
    ~Leaf() override {}
};
// A pure virtual may say `override`, and a declaration may be defined outside.
struct Mid : B { int h() override = 0; int g(int k) override; };
int Mid::g(int k) { return k + 100; }
struct Low : Mid { int h() final override { return 7; } };

int main() {
    int final = 3;
    int override = 2;
    D d; E e; Leaf leaf; Low low;
    B *pd = &d;
    B *pe = &e;
    B *pl = &leaf;
    B *plow = &low;
    int a = pd->f(), b = pd->g(5), c = pd->h();
    printf("%d %d %d\n", a, b, c);
    a = pe->f(); b = pe->g(6); c = pe->h();
    printf("%d %d %d\n", a, b, c);
    a = pl->f(); c = pl->h();
    printf("%d %d\n", a, c);
    b = plow->g(1); c = plow->h();
    printf("%d %d\n", b, c);
    printf("%d\n", final + override);
    return 0;
}
