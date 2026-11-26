// The shipped <memory>: unique_ptr with custom deleter? (skipped), release/reset/get, bool conversion, shared_ptr use_count, shared_ptr from unique_ptr? (skipped), nested ownership, arrays via delete[] in deleter (skipped).
#include <cstdio>
#include <memory>
#include <utility>
struct T { static int live; int v; T(int v) : v(v) { ++live; } ~T() { --live; } };
int T::live = 0;
std::unique_ptr<T> make(int v) { return std::unique_ptr<T>(new T(v)); }
int main() {
    { std::unique_ptr<T> a = make(1); std::unique_ptr<T> b; std::printf("%d %d %d ", (bool)a, (bool)b, a->v); b = std::move(a); std::printf("%d %d %d\n", (bool)a, b->v, T::live); }
    std::printf("%d\n", T::live);
    { std::unique_ptr<T> a(new T(2)); T *raw = a.release(); std::printf("%d %d ", a.get() == 0, raw->v); a.reset(raw); std::printf("%d ", a->v); a.reset(); std::printf("%d\n", T::live); }
    { std::shared_ptr<T> s(new T(3)); std::shared_ptr<T> s2 = s; std::printf("%d %d ", (int)s.use_count(), s2->v); { std::shared_ptr<T> s3 = s; std::printf("%d ", (int)s.use_count()); } std::printf("%d ", (int)s.use_count()); s2.reset(); std::printf("%d %d\n", (int)s.use_count(), T::live); }
    std::printf("%d\n", T::live);
    { std::shared_ptr<T> e; std::printf("%d %d\n", (bool)e, (int)e.use_count()); }
    return 0;
}
