// Bit-field read-modify-write under optimisation, unions reinterpreting (cl-defined), packed sequences of bit-fields crossing bytes, signed bit-fields sign extension, bit-field in a loop counter.
#include <cstdio>
#include <cstring>
struct BF { unsigned a : 4; unsigned b : 12; unsigned c : 16; int s : 5; unsigned long long w : 40; };
union U { float f; unsigned u; unsigned char b[4]; };
union Wide { unsigned long long ull; unsigned u[2]; double d; };
int main() {
    BF bf; std::memset(&bf, 0, sizeof bf); bf.a = 0xF; bf.b = 0xABC; bf.c = 0x1234; bf.s = -5; bf.w = 0xFFFFFFFFFFULL;
    std::printf("%u %u %u %d %llu %d\n", bf.a, bf.b, bf.c, bf.s, (unsigned long long)bf.w, (int)sizeof(BF));
    bf.a += 1; bf.b -= 1; bf.s += 1; std::printf("%u %u %d\n", bf.a, bf.b, bf.s);
    for (int i = 0; i < 20; ++i) ++bf.a; std::printf("%u\n", bf.a);
    U u; u.f = 1.0f; std::printf("%x %d %d\n", u.u, u.b[3], u.b[0]);
    u.u = 0x40490FDB; std::printf("%.5f\n", u.f);
    Wide w; w.d = -2.0; std::printf("%llx %x %x\n", w.ull, w.u[0], w.u[1]);
    unsigned char raw[4]; float g = 2.5f; std::memcpy(raw, &g, 4); std::printf("%d %d\n", raw[3], raw[2]);
    BF arr[3]; std::memset(arr, 0, sizeof arr); for (int i = 0; i < 3; ++i) arr[i].s = i - 1; std::printf("%d %d %d\n", arr[0].s, arr[1].s, arr[2].s);
    return 0;
}
