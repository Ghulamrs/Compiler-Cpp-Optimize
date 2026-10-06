// RTS6x's float bits, reduced: two small members walked in place one after the other, so the
// second's 8-byte parameter and the first's union share a frame key on the C6000. At -O2 the
// scalar took a register the union's member access never consulted, and the compiler stopped.
// Kept this small: a larger unit spends the inliner's budget and the two calls stay calls.
extern "C" int printf(const char *, ...);

class FloatBits {
public:
    static unsigned long long of(double d) { union { double d; unsigned long long u; } x; x.d = d; return x.u; }
    static double toDouble(unsigned long long u) { union { double d; unsigned long long u; } x; x.u = u; return x.d; }
    static unsigned long long of(float f) { union { float f; unsigned u; } x; x.f = f; return x.u; }
    static float toFloat(unsigned long long u) { union { float f; unsigned u; } x; x.u = (unsigned)u; return x.f; }
};

extern "C" float roundTrip(float x) { return FloatBits::toFloat(FloatBits::of(x)); }

int main() {
    float values[3] = { 1.5f, -0.25f, 1e30f };
    for (int i = 0; i < 3; i++) printf("%g\n", (double)roundTrip(values[i]));
    return 0;
}
