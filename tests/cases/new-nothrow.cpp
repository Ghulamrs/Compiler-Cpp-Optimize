// The half of <new> that needs no exception: `new (std::nothrow)` and the new
// handler, so that x86_64-windows - which refuses a class throw and catch by
// name and so cannot run new-header.cpp - still checks its `operator new(size_t,
// const std::nothrow_t &)`, `operator new[]` twin, `std::nothrow`,
// `std::set_new_handler` and `std::get_new_handler` against the CRT that
// defines them (libcmt and libcpmt, measured with dumpbin).
//
// [expr.new]/13: a null from the allocator initialises nothing, so the
// oversized `new (std::nothrow) Loud[n]` runs no constructor and the
// oversized `new (std::nothrow) int[n]()` zeroes nothing. The handler is
// called by the runtime on the failure and clears itself, which is what makes
// the second attempt answer null rather than loop; `handler ran` counts it.
#include <new>
#include <cstdio>
#include <cstddef>

static int handlerCalls = 0;
static void handler() { handlerCalls++; std::set_new_handler(0); }

static std::size_t big(int argc) { return argc > 0 ? (std::size_t)-1 / 2 : 1; }

struct Loud {
    int v;
    Loud(int n = 0);
};
Loud::Loud(int n) : v(n) { std::printf("Loud %d\n", n); }

int main(int argc, char **) {
    std::set_new_handler(handler);
    std::printf("handler set %d\n", std::get_new_handler() == handler);

    char *q = new (std::nothrow) char[big(argc)];
    std::printf("nothrow big is null %d, handler ran %d\n", q == 0, handlerCalls);
    std::set_new_handler(handler);
    Loud *lq = new (std::nothrow) Loud[big(argc) / sizeof(Loud)];
    std::printf("nothrow big class is null %d, handler ran %d\n", lq == 0, handlerCalls);
    int *zq = new (std::nothrow) int[big(argc) / sizeof(int)]();
    std::printf("nothrow big zeroed is null %d\n", zq == 0);
    std::printf("handler cleared %d\n", std::get_new_handler() == 0);

    int *r = new (std::nothrow) int(7);
    std::printf("nothrow int %d\n", *r);
    delete r;
    Loud *l = new (std::nothrow) Loud(3);
    std::printf("nothrow class %d\n", l->v);
    delete l;
    Loud *la = new (std::nothrow) Loud[2];
    std::printf("nothrow class array %d %d\n", la[0].v, la[1].v);
    delete[] la;
    int *z = new (std::nothrow) int[4]();
    std::printf("nothrow array %d %d\n", z[0], z[3]);
    delete[] z;
    return 0;
}
