// [over.ics.rank]/3.2.6: of A's constructor from B and B's conversion function to A, the one binding the less
// cv-qualified reference to the B wins; a non-const conversion function is no candidate for a const B.
#include <cstdio>

struct B1;
struct A1 {
    int how;
    A1(const B1 &) : how(1) {}
    explicit A1(int h) : how(h) {}
};
struct B1 {
    operator A1() { return A1(2); }
};

struct B2;
struct A2 {
    int how;
    A2(B2 &) : how(3) {}
    explicit A2(int h) : how(h) {}
};
struct B2 {
    operator A2() const { return A2(4); }
};

struct B3;
struct A3 {
    int how;
    explicit A3(const B3 &) : how(5) {}
    explicit A3(int h) : how(h) {}
};
struct B3 {
    operator A3() const { return A3(6); }
};

static int take1(A1 a) { return a.how; }
static int take2(A2 a) { return a.how; }
static int take3(A3 a) { return a.how; }

int main() {
    B1 b1;
    const B1 c1 = B1();
    B2 b2;
    const B2 c2 = B2();
    B3 b3;
    std::printf("%d %d %d %d %d %d\n", take1(b1), take1(c1), take1(B1()), take2(b2), take2(c2), take3(b3));
    return 0;
}
