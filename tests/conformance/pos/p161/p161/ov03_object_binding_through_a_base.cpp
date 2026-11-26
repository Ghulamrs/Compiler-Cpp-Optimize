// [over.ics.rank]/3.2.6: two members of one base bind the object with different added qualifiers, and the one adding
// fewer is better - `atomic`'s pairs; a member of the nearer class still wins.
#include <atomic>
#include <cstdio>
struct V {
    const char *f() { return "plain"; }
    const char *f() volatile { return "volatile"; }
    const char *f() const { return "const"; }
    V &operator++() { return *this; }
    V &operator++() volatile { return *const_cast<V *>(this); }
    int operator+=(int) { return 1; }
    int operator+=(int) volatile { return 2; }
};
struct W : V {};
int main() {
    W w;
    const W cw = W();
    volatile W vw;
    ++w;
    std::printf("%s %s %s %d\n", w.f(), cw.f(), vw.f(), w += 1);
    std::atomic<int> a(1);
    ++a;
    a += 2;
    a++;
    std::printf("%d\n", a.load());
    return 0;
}
