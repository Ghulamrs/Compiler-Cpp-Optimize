#include <cstdio>
#include <cstdlib>
#include <memory>
#include <utility>
struct Base { int b; Base(int b) : b(b) {} virtual ~Base() { std::printf("~Base %d\n", b); } };
struct Derived : Base { Derived(int b) : Base(b) {} ~Derived() { std::printf("~Derived %d\n", b); } };
struct Noisy { int n; Noisy() : n(0) {} ~Noisy() { std::printf("~Noisy %d\n", n); } };
struct Counted { int calls; Counted() : calls(0) {} void operator()(int *p) { calls++; std::printf("Counted deletes %d\n", *p); delete p; } };
void free_int(int *p) { std::printf("free_int %d\n", *p); delete p; }
int main() {
    std::printf("%d %d %d\n", (int)(sizeof(std::unique_ptr<int>) == sizeof(int *)), (int)(sizeof(std::unique_ptr<int[]>) == sizeof(int *)),
        (int)(sizeof(std::unique_ptr<int, void (*)(int *)>) == 2 * sizeof(int *)));
    {
        std::unique_ptr<int, void (*)(int *)> f(new int(7), free_int);
        std::printf("%d %d\n", *f, (int)(f.get_deleter() == free_int));
    }
    {
        std::unique_ptr<int, Counted> c(new int(8));
        c.reset(new int(9));
        std::printf("calls %d\n", c.get_deleter().calls);
    }
    {
        std::unique_ptr<Noisy[]> arr(new Noisy[3]);
        for (int i = 0; i < 3; i++) arr[i].n = 10 + i;
        std::printf("%d\n", arr[1].n);
        arr.reset();
        std::printf("after reset %d\n", (int)(arr.get() == 0));
    }
    {
        std::unique_ptr<Derived> d(new Derived(1));
        std::unique_ptr<Base> b(std::move(d));
        std::printf("%d %d\n", (int)(d.get() == 0), b->b);
        std::unique_ptr<Base> b2;
        b2 = std::unique_ptr<Derived>(new Derived(2));
        std::printf("%d %d\n", b2->b, (int)(b < b2 || b2 < b));
        b.swap(b2);
        std::printf("%d %d\n", b->b, b2->b);
        Base *raw = b2.release();
        std::printf("%d %d\n", raw->b, (int)(b2 == nullptr));
        delete raw;
    }
    std::default_delete<int> dd;
    dd(new int(3));
    std::default_delete<int[]> da;
    da(new int[4]);
    std::unique_ptr<int> e;
    std::printf("%d %d %d\n", (int)(e == nullptr), (int)(nullptr != e), (int)(bool)e);
    return 0;
}
