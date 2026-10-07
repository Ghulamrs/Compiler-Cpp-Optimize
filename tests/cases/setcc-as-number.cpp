// A comparison kept as a number: setcc writes one byte, and the zero extension after it is what
// makes the rest of the register 0. An extension read later by `imul $10, %ecx`, which writes the
// register as well, is a read of all four bytes - forward-values took the write alone and dropped it.
extern "C" int printf(const char *, ...);

static int asNumbers(int a, long long b) {
    int x = a != 0, y = b < 0, z = (a >= 0) + (b <= 0);
    return x * 100 + y * 10 + z;
}

static int sumOfTests(int a, int b) {
    int s = 7;
    int t = a < b;
    s += t;
    return s * 3 + (a == b);
}

int main() {
    printf("%d %d %d %d\n", asNumbers(0, 0), asNumbers(-5, -6), asNumbers(9, 3), asNumbers(-1, 0));
    printf("%d %d %d\n", sumOfTests(1, 2), sumOfTests(2, 1), sumOfTests(3, 3));
    return 0;
}
