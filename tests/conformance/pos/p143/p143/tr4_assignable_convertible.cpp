// [meta.unary.prop] is_assignable and its nothrow and trivially forms - `create<T>() = create<U>()` - and
// [meta.rel]/4 is_convertible: `To test() { return create<From>(); }`.
#include <cstdio>
struct Pod { int a; };
struct UserAssign { UserAssign &operator=(const UserAssign &) { return *this; } };
struct FromInt { FromInt(int) {} };
struct Explicit { explicit Explicit(int) {} };
struct ToInt { operator int() const { return 1; } };
struct ExplicitBool { explicit operator bool() const { return true; } };
struct NoAssign { NoAssign &operator=(const NoAssign &) = delete; };
struct NoThrowAssign { NoThrowAssign &operator=(int) noexcept { return *this; } };
struct Base { int b; };
struct Derived : Base {};
struct PrivBase : private Base {};
struct Abs { virtual void f() = 0; };
struct PrivCopy { PrivCopy() {} private: PrivCopy(const PrivCopy &); };
#define P(e) std::printf("%d", (int)(e))
int main() {
    P(__is_assignable(int &, int)); P(__is_assignable(int, int)); P(__is_assignable(const int &, int));
    P(__is_assignable(int &, double)); P(__is_assignable(int *&, int)); P(__is_assignable(Pod &, Pod));
    P(__is_assignable(Pod, Pod)); P(__is_assignable(Pod &, int)); P(__is_assignable(FromInt &, int));
    P(__is_assignable(NoAssign &, const NoAssign &)); P(__is_assignable(Base &, Derived));
    P(__is_assignable(Derived &, Base)); P(__is_assignable(void, int));
    std::printf(" ");
    P(__is_nothrow_assignable(int &, int)); P(__is_nothrow_assignable(Pod &, const Pod &));
    P(__is_nothrow_assignable(UserAssign &, const UserAssign &)); P(__is_nothrow_assignable(NoThrowAssign &, int));
    P(__is_trivially_assignable(int &, int)); P(__is_trivially_assignable(Pod &, const Pod &));
    P(__is_trivially_assignable(UserAssign &, const UserAssign &)); P(__is_trivially_assignable(NoThrowAssign &, int));
    P(__is_trivially_assignable(int, int));
    std::printf(" ");
    P(__is_convertible_to(int, long)); P(__is_convertible_to(int *, void *)); P(__is_convertible_to(void *, int *));
    P(__is_convertible_to(int, FromInt)); P(__is_convertible_to(int, Explicit)); P(__is_convertible_to(ToInt, int));
    P(__is_convertible_to(ExplicitBool, bool)); P(__is_convertible_to(Derived, Base)); P(__is_convertible_to(Base, Derived));
    P(__is_convertible_to(Derived *, Base *)); P(__is_convertible_to(PrivBase *, Base *));
    P(__is_convertible_to(Derived &, Base &)); P(__is_convertible_to(int, int &)); P(__is_convertible_to(int, const int &));
    P(__is_convertible_to(int &, int &&)); P(__is_convertible_to(int, int &&)); P(__is_convertible_to(void, void));
    P(__is_convertible_to(int, void)); P(__is_convertible_to(void, int)); P(__is_convertible_to(int[2], int *));
    P(__is_convertible_to(int *, int[2])); P(__is_convertible_to(void (), void (*)()));
    P(__is_convertible_to(void (), void (&)())); P(__is_convertible_to(Derived, Abs));
    P(__is_convertible_to(PrivCopy, PrivCopy)); P(__is_convertible_to(const PrivCopy &, const PrivCopy &));
    P(__is_convertible_to(double, int)); P(__is_convertible_to(int, bool)); P(__is_convertible_to(int *, bool));
    std::printf("\n");
}