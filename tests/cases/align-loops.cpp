// **Loops of every shape the align-loops pass pads**, run for their answers:
// a tiny loop, one that just fits a 64-byte line, one too large for it, a
// loop with two back edges (a `continue`), a do/while, nested loops whose
// outer head gets its entry run padded, and a loop in a function that is
// inlined. At -O2 each innermost loop that fits a line gets `.p2align 6,,L-1`
// in front; the pad holds no code, so every level must print the same.
extern "C" int printf(const char *, ...);

static int arr[100];

int tiny(int n) {
    int s = 0;
    for (int i = 0; i < n; ++i) s += i;
    return s;
}

int fits(int n) {
    long long s = 0;
    for (int i = 0; i < n; ++i) { arr[i] = i * 3; s += arr[i] ^ (i << 2); }
    return (int)(s & 0x7fffffff);
}

int large(int n) {
    long long s = 0;
    for (int i = 0; i < n; ++i) {
        int a = i * 7 % 13, b = (i + 5) * 11 % 17, c = a * b - (i >> 1);
        arr[i % 100] = c;
        s += c * 3 + a - b + arr[(i * 3) % 100];
    }
    return (int)(s & 0x7fffffff);
}

int twoBacks(int n) {
    int s = 0;
    for (int i = 0; i < n; ++i) {
        if (i % 3 == 0) continue;
        s += i;
    }
    return s;
}

int doWhile(int n) {
    int s = 0, i = 0;
    do { s += i * i; ++i; } while (i < n);
    return s;
}

int nested(int n) {
    int s = 0;
    for (int i = 0; i < n; ++i) {
        if (arr[i % 100] == 0) continue;
        for (int j = 0; j < i; ++j) s += (i ^ j) & 7;
    }
    return s;
}

static int inlined(int k) {
    int s = 0;
    for (int i = 0; i < k; ++i) s += i & 5;
    return s;
}

int main() {
    printf("%d %d %d\n", tiny(10), tiny(100), fits(100));
    printf("%d %d\n", large(500), twoBacks(100));
    printf("%d %d %d\n", doWhile(20), nested(60), inlined(33) + inlined(7));
    return 0;
}
