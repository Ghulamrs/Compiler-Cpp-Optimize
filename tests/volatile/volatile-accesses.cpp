// What a program does to volatile objects, shape by shape ([intro.execution]/8: "access to volatile objects are
// evaluated strictly according to the rules of the abstract machine"; C99 5.1.2.3, 6.7.3/6). Each function has an
// object of its own; run-volatile-c6747.ps1 runs the program on sim6747 with every object watched, and the loads
// and stores of each - how many, and the first sixty-four in their order - must be the same at -O0, -O1 and -O2.
// The program also prints what it computed. C++03 that is C++11, so that TI's cl6x can be asked the same.
#include <stdio.h>

volatile unsigned r01, r02, r03, r04, r05, r06, r07, r08, r09, r10;
volatile unsigned r11, r12, r13, r14, r15, r16, r17, r18, r19, r20;
volatile unsigned r21, r22, r23, r24, r25, r26, r27, r28, r29, r30;
volatile unsigned pair[2];
volatile unsigned char fifo[8];
volatile unsigned short half;
volatile signed char small;
struct Device { volatile unsigned control; volatile unsigned status; unsigned data[4]; };
Device device;
struct Whole { unsigned a, b; };
volatile Whole whole;

static unsigned plain[64];
static int calls;
static void other() { calls++; }
static unsigned two(unsigned a, unsigned b) { return a * 3 + b; }

// 1: read each turn of a loop, the address the same every turn
static unsigned readInLoop(int n) { unsigned s = 0; for (int i = 0; i < n; i++) s += r01 + (unsigned)i; return s; }
// 2: two reads in one expression, and two statements
static unsigned readTwice() { unsigned a = r02 + r02; unsigned b = r02; unsigned c = r02; return a + b + c; }
// 3: a read whose value is thrown away is still a read
static void readDiscarded() { r03; (void)r03; unsigned unused = r03; (void)unused; }
// 4: every store, the same value twice too
static void storeTwice() { r04 = 1; r04 = 2; r04 = 2; r04 = 3; }
// 5: read-modify-write
static void setAndClear() { r05 |= 0x10; r05 &= ~1u; r05 ^= 0xff; }
// 6: increments
static unsigned increments() { unsigned a = r06++; unsigned b = ++r06; r06 += 2; r06--; return a * 100 + b; }
// 7: a polling loop
static int poll(int limit) { int n = 0; while ((r07 & 0x80) == 0 && n < limit) n++; return n; }
// 8: a volatile local
static int local(int n) { volatile int v = 0; for (int i = 0; i < n; i++) v += i; return v; }
// 9: through a pointer to volatile
static unsigned through(volatile unsigned *p) { *p = 5; unsigned a = *p; p[0] = a + 1; return *p + p[0]; }
// 10: a store loop over a volatile array is eight byte stores, a read loop eight byte loads
static unsigned bytes() { for (int i = 0; i < 8; i++) fifo[i] = (unsigned char)(i + 1); unsigned s = 0; for (int i = 0; i < 8; i++) s += fifo[i]; return s; }
// 11: conditions - the arm not taken is not accessed
static unsigned arms(int c) { unsigned x = 0; if (c) r11 = 1; else x = r11; x += c ? r11 : 7u; if (c && r11) x++; if (c || r11) x += 2; return x; }
// 12: a loop bound read each turn
static int bound() { int n = 0; for (unsigned i = 0; i < r12; i++) n++; return n; }
// 13: the same store each turn, nothing else in the loop but arithmetic
static unsigned sameStore(int n) { unsigned s = 0; for (int i = 0; i < n; i++) { r13 = 7; s += (unsigned)i * 3; } return s; }
// 14: narrower objects
static int narrow() { half = 0x1234; half++; small = -3; small -= 1; return half + small; }
// 15: a call between two stores, and a read either side of one
static unsigned aroundCall() { r15 = 1; other(); r15 = 2; unsigned a = r15; other(); return a + r15; }
// 16: members of a structure, one volatile and its neighbours not
static unsigned members() { device.control = 3; device.data[1] = 9; unsigned s = device.status; s += device.status; device.control |= 4; return s + device.data[1]; }
// 17: two objects in one array, in their order
static unsigned inOrder() { pair[0] = 1; pair[1] = 2; unsigned a = pair[1]; unsigned b = pair[0]; pair[1] = a + b; return pair[0] + pair[1]; }
// 18: the comma operator and nested expressions
static unsigned comma() { unsigned x = (r18, r18); x += (r18 = 4, r18 + 1); return x; }
// 19: a switch on a volatile, a return of one, arguments of a call
static unsigned asOperand() { switch (r19) { case 0: r19 = 9; break; default: r19 = 1; } return two(r19, r19); }
// 20: an index that is volatile
static unsigned index() { r20 = 3; plain[r20] = 11; return plain[r20] + plain[3]; }
// 21: a do-while on a volatile condition
static int doWhile() { int n = 0; do { n++; } while (r21 == 0 && n < 6); return n; }
// 22: a delay loop - reads nobody uses
static void delay(int n) { for (int i = 0; i < n; i++) (void)r22; }
// 23: a volatile loop counter
static int counter() { int n = 0; for (r23 = 0; r23 < 5; r23++) n += 2; return n; }
// 24: a volatile beside a loop the optimiser would otherwise take for itself
static unsigned beside() { unsigned s = 0; for (int i = 0; i < 64; i++) plain[i] = (unsigned)i; r24 = 1; for (int i = 0; i < 64; i++) s += plain[i]; r24 = s; return s + r24; }
// 25: read, then written with what was read, twice
static void copyBack() { r25 = r25; r25 = r25; }
// 26: a volatile parameter
static int parameter(volatile int x) { x += 1; return x + x; }
// 27: the same address read into two variables with a store between
static unsigned between() { unsigned a = r27; r27 = a + 1; unsigned b = r27; r27 = b + 1; return a * 10 + b; }
// 28: a loop that leaves on a volatile, with a store in its body
static int leaveOn() { int n = 0; for (;;) { r28 = (unsigned)n; if (r28 >= 4) break; n++; } return n; }
// 29: a whole volatile structure, member by member
static unsigned structure() { whole.a = 1; whole.b = 2; return whole.a + whole.b * 10; }
// 30: logical operators and a conditional as a statement
static unsigned logical(int c) { unsigned x = 0; c && (r30 = 5); c || (r30 = 6); x = !r30; x += r30 ? 1 : 0; return x; }

int main()
{
    r07 = 0; r12 = 6; r21 = 0;
    printf("%u %u\n", readInLoop(10), readTwice());
    readDiscarded(); storeTwice(); setAndClear();
    printf("%u %d %d %d\n", increments(), poll(5), local(10), parameter(4));
    printf("%u %u\n", through(&r09), bytes());
    printf("%u %u %d\n", arms(0), arms(1), bound());
    printf("%u %d %u\n", sameStore(5), narrow(), aroundCall());
    printf("%u %u %u\n", members(), inOrder(), comma());
    printf("%u %u %d\n", asOperand(), index(), doWhile());
    delay(7);
    printf("%d %u %u %d\n", counter(), beside(), between(), leaveOn());
    copyBack();
    printf("%u %u %u %d\n", structure(), logical(0), logical(1), calls);
    return 0;
}
