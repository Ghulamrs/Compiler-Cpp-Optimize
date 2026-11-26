// [meta.unary.prop]: is_constructible and its nothrow and trivially forms - `T t(create<Args>()...);` well-formed
// from a context unrelated to T - and the destructible ones.
#include <cstdio>
struct Pod { int a; };
struct FromInt { FromInt(int) {} };
struct Explicit { explicit Explicit(int) {} };
struct Two { Two(int, double) {} };
struct NoDefault { NoDefault(int) {} };
struct PrivCtor { private: PrivCtor() {} };
struct DelCopy { DelCopy() {} DelCopy(const DelCopy &) = delete; };
struct MoveOnly { MoveOnly() {} MoveOnly(MoveOnly &&) {} };
struct NoThrow { NoThrow() noexcept {} NoThrow(int) noexcept {} NoThrow(double) {} };
struct Abs { virtual void f() = 0; };
struct PrivDtor { private: ~PrivDtor() {} };
struct ThrowDtor { ~ThrowDtor() noexcept(false) {} };
struct ToInt { operator int() const { return 1; } };
struct ExplicitBool { explicit operator bool() const { return true; } };
struct Base { int b; };
struct Derived : Base { int d; };
struct UserCopy { UserCopy() {} UserCopy(const UserCopy &) {} };
struct Nested { FromInt f; };
#define P(e) std::printf("%d", (int)(e))
int main() {
    P(__is_constructible(int)); P(__is_constructible(int, int)); P(__is_constructible(int, double));
    P(__is_constructible(int, int, int)); P(__is_constructible(int *, int)); P(__is_constructible(int *, void *));
    P(__is_constructible(void *, int *)); P(__is_constructible(int &)); P(__is_constructible(int &, int &));
    P(__is_constructible(int &, int)); P(__is_constructible(const int &, int)); P(__is_constructible(int &&, int));
    P(__is_constructible(int &&, int &)); P(__is_constructible(void)); P(__is_constructible(int[3]));
    P(__is_constructible(int[])); P(__is_constructible(int, void));
    std::printf(" ");
    P(__is_constructible(Pod)); P(__is_constructible(Pod, Pod)); P(__is_constructible(Pod, int));
    P(__is_constructible(FromInt, int)); P(__is_constructible(FromInt)); P(__is_constructible(Explicit, int));
    P(__is_constructible(Two, int, double)); P(__is_constructible(Two, int)); P(__is_constructible(NoDefault));
    P(__is_constructible(PrivCtor)); P(__is_constructible(DelCopy, const DelCopy &));
    P(__is_constructible(MoveOnly, MoveOnly)); P(__is_constructible(MoveOnly, MoveOnly &));
    P(__is_constructible(Abs)); P(__is_constructible(PrivDtor));
    P(__is_constructible(int, ToInt)); P(__is_constructible(bool, ExplicitBool)); P(__is_constructible(Base, Derived));
    P(__is_constructible(Derived, Base)); P(__is_constructible(Base &, Derived &)); P(__is_constructible(Nested));
    P(__is_constructible(Pod[2]));
    std::printf(" ");
    P(__is_nothrow_constructible(int)); P(__is_nothrow_constructible(int, double)); P(__is_nothrow_constructible(NoThrow));
    P(__is_nothrow_constructible(NoThrow, int)); P(__is_nothrow_constructible(NoThrow, double));
    P(__is_nothrow_constructible(FromInt, int)); P(__is_nothrow_constructible(Pod, const Pod &));
    P(__is_nothrow_constructible(UserCopy, const UserCopy &)); P(__is_nothrow_constructible(int &, int &));
    std::printf(" ");
    P(__is_trivially_constructible(int)); P(__is_trivially_constructible(int, long)); P(__is_trivially_constructible(Pod));
    P(__is_trivially_constructible(Pod, const Pod &)); P(__is_trivially_constructible(Pod, Pod));
    P(__is_trivially_constructible(UserCopy)); P(__is_trivially_constructible(UserCopy, const UserCopy &));
    P(__is_trivially_constructible(FromInt, int)); P(__is_trivially_constructible(Base, Derived));
    std::printf(" ");
    P(__is_destructible(int)); P(__is_destructible(int &)); P(__is_destructible(void)); P(__is_destructible(Pod));
    P(__is_destructible(PrivDtor)); P(__is_destructible(int[2]));
    P(__is_destructible(int[])); P(__is_destructible(Abs)); P(__is_destructible(ThrowDtor));
    P(__is_nothrow_destructible(Pod)); P(__is_nothrow_destructible(ThrowDtor)); P(__is_nothrow_destructible(UserCopy));
    P(__is_nothrow_destructible(PrivDtor));
    P(__is_trivially_destructible(int)); P(__is_trivially_destructible(Pod)); P(__is_trivially_destructible(ThrowDtor));
    P(__is_trivially_destructible(UserCopy)); P(__is_trivially_destructible(int &));
    std::printf("\n");
}