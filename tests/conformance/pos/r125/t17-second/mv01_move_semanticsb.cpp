// std::move and move constructors: resources stolen, source left empty; move assignment; self-move-assignment avoided; move of locals on return ([class.copy]/32 implicit move); swap via moves.
#include <cstdio>
#include <utility>
#include <cstring>
#include <cstdlib>
struct Buf {
    char *p; int n;
    Buf(const char *s) : p((char *)std::malloc(std::strlen(s) + 1)), n((int)std::strlen(s)) { std::strcpy(p, s); }
    Buf(const Buf &o) : p((char *)std::malloc(o.n + 1)), n(o.n) { std::strcpy(p, o.p); std::printf("copy "); }
    Buf(Buf &&o) : p(o.p), n(o.n) { o.p = 0; o.n = 0; std::printf("move "); }
    Buf &operator=(Buf &&o) { if (this != &o) { std::free(p); p = o.p; n = o.n; o.p = 0; o.n = 0; } std::printf("massign "); return *this; }
    Buf &operator=(const Buf &o) { if (this != &o) { std::free(p); p = (char *)std::malloc(o.n + 1); n = o.n; std::strcpy(p, o.p); } std::printf("cassign "); return *this; }
    ~Buf() { std::free(p); }
};
Buf make(const char *s) { Buf b(s); return b; }
Buf pass(Buf b) { return b; }
int main() {
    Buf a("hello"); Buf b = std::move(a);
    std::printf("| %d %s %d\n", a.p == 0, b.p, b.n);
    Buf c("x"); c = std::move(b); std::printf("| %s %d\n", c.p, b.p == 0);
    Buf d = make("made"); std::printf("| %s\n", d.p);
    Buf e = pass(std::move(d)); std::printf("| %s %d\n", e.p, d.p == 0);
    Buf f("f"); Buf g("g"); std::swap(f, g); std::printf("| %s %s\n", f.p, g.p);
    Buf h("h"); h = h; std::printf("| %s\n", h.p);
    return 0;
}
