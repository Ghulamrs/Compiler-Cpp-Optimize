// [except.spec]/14: an implicit special member allows no exception when nothing it invokes does - a polymorphic
// class's copy, move and default, and their assignments; one whose member may throw allows exceptions too.
#include <cstdio>
#include <type_traits>
struct Poly { virtual ~Poly() {} };
struct Derived : Poly { int y; };
struct Plain { int x; };
struct Thrower { Thrower() {} Thrower(const Thrower &) {} Thrower &operator=(const Thrower &) { return *this; } };
struct Holds : Poly { Thrower t; };
struct Init : Poly { int n = 4; };
int main() {
    std::printf("%d %d %d %d\n", (int)__is_nothrow_constructible(Poly, const Poly &),
                (int)__is_nothrow_constructible(Poly, Poly &&), (int)__is_nothrow_assignable(Poly &, const Poly &),
                (int)__is_nothrow_assignable(Poly &, Poly &&));
    std::printf("%d %d %d %d\n", (int)std::is_nothrow_copy_constructible<Derived>::value,
                (int)std::is_nothrow_move_constructible<Derived>::value,
                (int)std::is_nothrow_copy_assignable<Derived>::value, (int)std::is_nothrow_default_constructible<Poly>::value);
    std::printf("%d %d %d %d\n", (int)std::is_nothrow_copy_constructible<Holds>::value,
                (int)std::is_nothrow_copy_assignable<Holds>::value, (int)std::is_nothrow_copy_constructible<Plain>::value,
                (int)std::is_nothrow_copy_constructible<Init>::value);
    return 0;
}
