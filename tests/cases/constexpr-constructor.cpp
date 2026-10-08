// A `constexpr` constructor - [dcl.constexpr]/3, /4 - may always run at run time, and here it
// does: the class is built by the ordinary constructor, and a constexpr member function on it
// is called like any other. What the evaluator cannot do is make a constexpr *object* of
// class type, which constexpr-class-object-refused holds. clang agrees on every line.
extern "C" int printf(const char *, ...);

struct P {
    int x, y;
    constexpr P() : x(0), y(0) {}
    constexpr P(int a, int b) : x(a), y(b) {}
    constexpr int sum() const { return x + y; }
};

struct Q {
    int v;
    explicit constexpr Q(int a) : v(a * 2) {}
};

struct R : P {
    int z;
    constexpr R(int a) : P(a, a + 1), z(a * 3) {}
};

constexpr int twice(int n) { return n * 2; }

int main() {
    P p(3, 4);
    const P zero;
    Q q(5);
    R r(2);
    int s = p.sum();
    int z = zero.sum();
    printf("%d %d %d\n", s, z, q.v);
    int rs = r.sum();
    printf("%d %d\n", rs, r.z);
    int arr[twice(3)];
    int six = twice(3);
    printf("%d %d\n", (int)(sizeof arr / sizeof arr[0]), six);
    return 0;
}
