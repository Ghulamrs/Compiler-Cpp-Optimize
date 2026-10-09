// A range-based `for` whose iterator is a class - [stmt.ranged]/1: the loop is
// `for (auto __b = r.begin(), __e = r.end(); __b != __e; ++__b) { T x = *__b; }`,
// and with a class iterator `!=`, `++` and `*` are the iterator's own operators,
// found by the ordinary operator resolution. The iterators are built once each
// and destroyed after the loop: the live count is back to zero after each one.
// Counted rather than traced by address - a class returned by value moves here
// where clang elides, and both are allowed. Refused until now.
extern "C" int printf(const char *, ...);

static int live = 0;

struct Cursor {
    int *p;
    Cursor(int *q);
    Cursor(const Cursor &o);
    ~Cursor();
    int &operator*() const;
    Cursor &operator++();
};
Cursor::Cursor(int *q) : p(q) { live++; }
Cursor::Cursor(const Cursor &o) : p(o.p) { live++; }
Cursor::~Cursor() { live--; }
int &Cursor::operator*() const { return *p; }
Cursor &Cursor::operator++() { ++p; return *this; }
bool operator!=(const Cursor &a, const Cursor &b) { return a.p != b.p; }

struct Bag {
    int d[4];
    Cursor begin();
    Cursor end();
};
Cursor Bag::begin() { return Cursor(d); }
Cursor Bag::end() { return Cursor(d + 4); }

struct Step {
    const char *s;
    char operator*() const { return *s; }
    Step &operator++() { s += 2; return *this; }
    bool operator!=(const Step &o) const { return s != o.s; }
};
struct Evens {
    const char *text;
    Step begin() const { Step k; k.s = text; return k; }
    Step end() const { Step k; k.s = text + 8; return k; }
};

int main(void) {
    Bag b;
    for (int i = 0; i < 4; i++) b.d[i] = i + 1;
    for (int n : b) printf("%d ", n);
    printf("| live %d\n", live);
    for (int &n : b) n *= 10;
    for (auto n : b) printf("%d ", n);
    printf("\n");
    const Evens e = { "abcdefgh" };
    for (char c : e) printf("%c", c);
    printf("\n");
    int total = 0;
    for (const auto &n : b) {
        if (n == 30) break;
        total += n;
    }
    printf("%d | live %d\n", total, live);
    return 0;
}
