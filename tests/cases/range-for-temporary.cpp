// A range-based `for` over a temporary - [stmt.ranged]/1 binds the range to
// `auto &&__range`, and [class.temporary]/5 keeps the temporary alive to the
// end of the loop: built once, destroyed once, after the last turn and before
// the statement that follows - counted, and back to zero after each loop.
// Refused until now.
extern "C" int printf(const char *, ...);

static int live = 0;

struct Bag {
    int d[3];
    Bag(int base);
    Bag(const Bag &o);
    ~Bag();
    int *begin() { return d; }
    int *end() { return d + 3; }
};
Bag::Bag(int base) { for (int i = 0; i < 3; i++) d[i] = base + i; live++; }
Bag::Bag(const Bag &o) { for (int i = 0; i < 3; i++) d[i] = o.d[i]; live++; }
Bag::~Bag() { live--; printf("~Bag "); }

Bag make(int base) { return Bag(base); }

struct Plain {
    int v[2];
    const int *begin() const { return v; }
    const int *end() const { return v + 2; }
};
Plain plain(void) { Plain p; p.v[0] = 7; p.v[1] = 8; return p; }

// An exception leaving the loop destroys the range on its way out, as the end does.
static void boom(int n) { if (n == 41) throw n; }
static void walk(void) { for (int n : Bag(40)) boom(n); }
static void guarded(void) {
    try { walk(); } catch (int k) { printf("caught %d | live %d\n", k, live); }
}

int main(void) {
    for (int n : make(10)) printf("%d ", n);
    printf("| live %d\n", live);
    for (int n : Bag(20)) printf("%d ", n);
    printf("| live %d\n", live);
    for (int n : plain()) printf("%d ", n);
    printf("\n");
    guarded();
    return 0;
}
