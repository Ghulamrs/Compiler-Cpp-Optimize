// [class.copy]: implicit copy/move generation; a user-declared copy ctor suppresses the implicit move; = default; = delete; move of members; which one runs.
#include <cstdio>
#include <utility>
struct Trace {
    Trace() { std::printf("def "); }
    Trace(const Trace &) { std::printf("copy "); }
    Trace(Trace &&) { std::printf("move "); }
    Trace &operator=(const Trace &) { std::printf("cassign "); return *this; }
    Trace &operator=(Trace &&) { std::printf("massign "); return *this; }
    ~Trace() {}
};
struct Implicit { Trace t; };                         // gets implicit move
struct UserCopy { Trace t; UserCopy() {} UserCopy(const UserCopy &o) : t(o.t) {} };   // no implicit move: move falls back to copy
struct Defaulted { Trace t; Defaulted() = default; Defaulted(Defaulted &&) = default; Defaulted(const Defaulted &) = default; };
struct NoCopy { NoCopy() {} NoCopy(const NoCopy &) = delete; NoCopy &operator=(const NoCopy &) = delete; NoCopy(NoCopy &&) { std::printf("nc-move "); } };
struct UserDtor { Trace t; ~UserDtor() {} };          // user dtor: no implicit move, copy used
int main() {
    Implicit a; Implicit b = std::move(a); Implicit c = b; c = std::move(b); c = a; std::printf("\n");
    UserCopy u; UserCopy v = std::move(u); std::printf("\n");
    Defaulted d; Defaulted e = std::move(d); Defaulted f = e; std::printf("\n");
    NoCopy n; NoCopy m = std::move(n); std::printf("\n");
    UserDtor x; UserDtor y = std::move(x); std::printf("\n");
    return 0;
}
