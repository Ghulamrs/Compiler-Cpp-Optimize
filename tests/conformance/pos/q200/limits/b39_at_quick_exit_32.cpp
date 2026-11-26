#include <cstdio>
#include <cstdlib>
static int ran = 0;
static void f0() { ran++; std::printf("%d\n", ran); std::fflush(stdout); }
static void f1() { ran++; }
static void f2() { ran++; }
static void f3() { ran++; }
static void f4() { ran++; }
static void f5() { ran++; }
static void f6() { ran++; }
static void f7() { ran++; }
static void f8() { ran++; }
static void f9() { ran++; }
static void f10() { ran++; }
static void f11() { ran++; }
static void f12() { ran++; }
static void f13() { ran++; }
static void f14() { ran++; }
static void f15() { ran++; }
static void f16() { ran++; }
static void f17() { ran++; }
static void f18() { ran++; }
static void f19() { ran++; }
static void f20() { ran++; }
static void f21() { ran++; }
static void f22() { ran++; }
static void f23() { ran++; }
static void f24() { ran++; }
static void f25() { ran++; }
static void f26() { ran++; }
static void f27() { ran++; }
static void f28() { ran++; }
static void f29() { ran++; }
static void f30() { ran++; }
static void f31() { ran++; }
int main() {
    std::at_quick_exit(f0);
    std::at_quick_exit(f1);
    std::at_quick_exit(f2);
    std::at_quick_exit(f3);
    std::at_quick_exit(f4);
    std::at_quick_exit(f5);
    std::at_quick_exit(f6);
    std::at_quick_exit(f7);
    std::at_quick_exit(f8);
    std::at_quick_exit(f9);
    std::at_quick_exit(f10);
    std::at_quick_exit(f11);
    std::at_quick_exit(f12);
    std::at_quick_exit(f13);
    std::at_quick_exit(f14);
    std::at_quick_exit(f15);
    std::at_quick_exit(f16);
    std::at_quick_exit(f17);
    std::at_quick_exit(f18);
    std::at_quick_exit(f19);
    std::at_quick_exit(f20);
    std::at_quick_exit(f21);
    std::at_quick_exit(f22);
    std::at_quick_exit(f23);
    std::at_quick_exit(f24);
    std::at_quick_exit(f25);
    std::at_quick_exit(f26);
    std::at_quick_exit(f27);
    std::at_quick_exit(f28);
    std::at_quick_exit(f29);
    std::at_quick_exit(f30);
    std::at_quick_exit(f31);
    std::quick_exit(0);
}
