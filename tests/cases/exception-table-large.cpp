// A function whose code passes 64 KB: its exception scopes lie past what a
// 16-bit descriptor holds, so the C6000 table is TI's pr2, 32-bit scope.
// A catch and a cleanup far into it, and a throw that unwinds through it.
extern "C" int printf(const char *, ...);

struct S { S(int n); ~S(); int v; };
S::S(int n) : v(n) { printf("+S%d\n", n); }
S::~S() { printf("-S%d\n", v); }

static int at = -1;
int step(int x) {
    if (x == at) throw x;
    return (x * 7 + 3) & 15;
}

#define C1(k)   v += step(n + (k));
#define C10(k)  C1(k) C1(k + 1) C1(k + 2) C1(k + 3) C1(k + 4) C1(k + 5) C1(k + 6) C1(k + 7) C1(k + 8) C1(k + 9)
#define C100(k) C10(k) C10(k + 10) C10(k + 20) C10(k + 30) C10(k + 40) C10(k + 50) C10(k + 60) C10(k + 70) C10(k + 80) C10(k + 90)
#define C500(k) C100(k) C100(k + 100) C100(k + 200) C100(k + 300) C100(k + 400)

// The throw is caught here: the call that throws sits far past 64 KB.
int caught(int n) {
    S s(1);
    int v = 0;
    try {
        C500(0) C500(500) C100(1000)
    } catch (int e) {
        v = -e;
    }
    return v + s.v;
}

// Nothing is caught here: its cleanup runs, and the throw goes on to main.
int passed(int n) {
    S s(2);
    int v = 0;
    C500(0) C500(500) C100(1000)
    return v + s.v;
}

int main() {
    int keep = 12345;
    printf("%d\n", caught(0));
    at = 1090;
    printf("%d\n", caught(0));
    at = 7;
    printf("%d\n", caught(0));
    at = 1095;
    try {
        printf("%d\n", passed(0));
    } catch (int e) {
        printf("main caught %d, keep %d\n", e, keep);
    }
    at = -1;
    printf("%d keep %d\n", passed(0), keep + 1);
    return 0;
}
