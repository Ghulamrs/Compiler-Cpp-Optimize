#include <cstdio>

// [dcl.constexpr]/3: typedefs and static_asserts beside the one return.
template <class T> constexpr T twice(T v) {
    typedef T value;
    static_assert(sizeof(value) > 0, "complete");
    return value(v * 2);
}

// [expr.log.and]: the right operand is not evaluated, so its overflow is no matter.
template <long long A, long long B> struct fits {
    static const bool value = (B >= 0 && A <= 9223372036854775807LL - B) || (B < 0 && A >= (-9223372036854775807LL - 1) - B);
};
static_assert(fits<5, -7>::value, "negative B");
static_assert(!fits<9223372036854775807LL, 1>::value, "overflow");
static_assert(true || (1 / 0 == 0), "unevaluated division");

template <class T> class Box;
template <class T> class Key {
    friend class Box<T>;
    T secret;
public:
    explicit Key(T s) : secret(s) {}
};
template <class T> class Box {
public:
    T open(const Key<T> &k) const { return k.secret; }
};

template <class T> struct Wrap {
    T v;
    template <class U> constexpr explicit Wrap(const U &u) : v(static_cast<T>(u)) {}
};

int main() {
    constexpr int t = twice(21);
    static_assert(t == 42, "constexpr");
    Key<int> k(7);
    Box<int> b;
    Wrap<long> w(3);
    std::printf("%d %d %ld\n", t, b.open(k), w.v);
    return 0;
}
