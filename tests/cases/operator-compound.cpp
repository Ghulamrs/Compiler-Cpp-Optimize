// `a += b` on a class, where the class declares `operator+=`.
//
// **A compound assignment on a class is that operator alone.** For a built-in
// operand `a += b` is built by reading the target back, combining and storing;
// [over.ass] makes each `@=` a member of its own, and a class that declares
// `operator+` and `operator=` but not `operator+=` cannot be written `+=` at
// all - which operator-compound-refused.cpp is the other half of.
//
// The one that matters for a library is `+=` on a string, built one character
// at a time; the rest are here because they are the same rule, and because a
// list of ten written once is where a list of nine hides.

extern "C" int printf(const char *, ...);

struct S {
    int v;
    S &operator+=(const S &o) { v += o.v; return *this; }
    S &operator+=(int n) { v += n * 100; return *this; }   // and they overload
    S &operator-=(int n) { v -= n; return *this; }
    S &operator*=(int n) { v *= n; return *this; }
    S &operator/=(int n) { v /= n; return *this; }
    S &operator%=(int n) { v = v % n; return *this; }
    S &operator&=(int n) { v = v & n; return *this; }
    S &operator|=(int n) { v = v | n; return *this; }
    S &operator^=(int n) { v = v ^ n; return *this; }
    S &operator<<=(int n) { v = v << n; return *this; }
    S &operator>>=(int n) { v = v >> n; return *this; }
};

int main(void) {
    S a; a.v = 1;
    S b; b.v = 2;
    a += b;                       // 3, the class overload
    a += 1;                       // 103, the int one
    a -= 3;                       // 100
    a *= 3;                       // 300
    a /= 4;                       // 75
    a %= 40;                      // 35
    printf("%d ", a.v);
    a &= 12;                      // 0b100011 & 0b1100 = 0
    a |= 5;                       // 5
    a ^= 3;                       // 6
    a <<= 4;                      // 96
    a >>= 2;                      // 24
    int before = a.v;
    int after = (a += b).v;
    printf("%d %d\n", before, after);
    return 0;
}
