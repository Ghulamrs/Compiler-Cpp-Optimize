// <new>, [support.dynamic], over the platform's own runtime.
//
// The test that proves the header names the runtime's objects rather than a
// copy of them: an allocation that fails inside the real `operator new` is
// caught here as `std::bad_alloc &` and again as `std::exception &`, which
// works only if the type_info cpp11 names in the catch is the one libc++abi
// or libstdc++ threw with - `_ZTISt9bad_alloc`, and the runtime's
// `_ZTISt9exception` under it. Then the new handler, called by the runtime
// on that failure and read back through `get_new_handler`; `new
// (std::nothrow)` of the same size answering null and initialising nothing;
// a nothrow scalar and a nothrow class that succeed and are initialised; and
// the two classes thrown by the program itself.
//
// The absurd size is computed at run time: clang refuses `new char[N]` for
// a constant N it can see is too large, and the point is the runtime's answer.
// `e.what()` is the runtime's string. libc++abi's are the .expected; libstdc++
// spells bad_array_new_length with its `std::` and the Microsoft runtime both
// in words - measured on each box - so said() maps them back to libc++abi's.
#include <new>
#include <cstdio>
#include <cstddef>
#include <cstring>

static const char *said(const char *w) {
#ifdef _WIN32
    if (std::strcmp(w, "bad allocation") == 0) return "std::bad_alloc";
    if (std::strcmp(w, "bad array new length") == 0) return "bad_array_new_length";
#endif
    if (std::strcmp(w, "std::bad_array_new_length") == 0) return "bad_array_new_length";
    return w;
}

static int handlerCalls = 0;
static void handler() { handlerCalls++; std::set_new_handler(0); }

static std::size_t big(int argc) { return argc > 0 ? (std::size_t)-1 / 2 : 1; }

struct Loud {
    int v;
    Loud(int n = 0);
};
Loud::Loud(int n) : v(n) { if (n) std::printf("Loud %d\n", n); }

int main(int argc, char **) {
    std::set_new_handler(handler);
    std::new_handler h = std::get_new_handler();
    std::printf("handler set %d\n", h == handler);

    try {
        char *p = new char[big(argc)];
        std::printf("got %p\n", (void *)p);
    } catch (std::bad_alloc &e) {
        std::printf("bad_alloc: %s\n", said(e.what()));
    }
    std::printf("handler ran %d\n", handlerCalls);

    try {
        char *p = new char[big(argc)];
        std::printf("got %p\n", (void *)p);
    } catch (std::exception &e) {
        std::printf("exception: %s\n", said(e.what()));
    }

    char *q = new (std::nothrow) char[big(argc)];
    std::printf("nothrow big is null %d\n", q == 0);
    Loud *lq = new (std::nothrow) Loud[big(argc) / sizeof(Loud)];
    std::printf("nothrow big class is null %d\n", lq == 0);

    int *r = new (std::nothrow) int(7);
    std::printf("nothrow int %d\n", *r);
    delete r;
    Loud *l = new (std::nothrow) Loud(3);
    std::printf("nothrow class %d\n", l->v);
    delete l;
    int *z = new (std::nothrow) int[4]();
    std::printf("nothrow array %d %d\n", z[0], z[3]);
    delete[] z;

    try { throw std::bad_alloc(); }
    catch (std::exception &e) { std::printf("thrown: %s\n", said(e.what())); }
    try { throw std::bad_array_new_length(); }
    catch (std::bad_alloc &e) { std::printf("thrown length: %s\n", said(e.what())); }
    return 0;
}
