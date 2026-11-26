// [dcl.constexpr]/9: constexpr makes the pointer const, not what it points to - `int *const` - unless const is
// written too; an array of pointers has const pointers. Seen by decltype and by overload resolution.
#include <cstdio>

template <class A, class B> struct same { static const bool v = false; };
template <class A> struct same<A, A> { static const bool v = true; };

int g = 1, h = 2;
constexpr int *p = &g;
constexpr const int *cp = &h;
constexpr int *ps[2] = { &g, &h };
static_assert(same<decltype(p), int *const>::v, "p");
static_assert(same<decltype(cp), const int *const>::v, "cp");
static_assert(same<decltype(ps), int *const[2]>::v, "ps");

struct Holder {
    static constexpr int *member = &g;
};
constexpr int *Holder::member;
static_assert(same<decltype(Holder::member), int *const>::v, "member");

static int f(int *) { return 1; }
static int f(const int *) { return 2; }

int main() {
    constexpr int *local = &h;
    static_assert(same<decltype(local), int *const>::v, "local");
    *ps[1] = 5;
    *Holder::member = 7;
    std::printf("%d %d %d %d %d %d\n", f(p), f(cp), f(local), f(Holder::member), g, h);
    return 0;
}
