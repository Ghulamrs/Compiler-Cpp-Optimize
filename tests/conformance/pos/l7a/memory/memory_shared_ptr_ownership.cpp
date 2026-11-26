#include <cstdio>
#include <exception>
#include <memory>

#include <utility>
struct Base { int b; Base(int b) : b(b) {} virtual ~Base() { std::printf("~Base %d\n", b); } };
struct Derived : Base { Derived(int b) : Base(b) {} ~Derived() { std::printf("~Derived %d\n", b); } };
struct Node : std::enable_shared_from_this<Node> {
    int v;
    Node(int v) : v(v) {}
    ~Node() { std::printf("~Node %d\n", v); }
};
struct Pair { int a; int b; Pair(int a, int b) : a(a), b(b) {} };
struct Tell { void operator()(int *p) const { std::printf("Tell deletes %d\n", *p); delete p; } };
int main() {
    std::printf("%d\n", (int)(sizeof(std::shared_ptr<int>) == 2 * sizeof(void *)));
    {
        std::shared_ptr<Base> b(new Derived(1));
        std::shared_ptr<Base> c = b;
        std::printf("%ld %d %d\n", b.use_count(), (int)b.unique(), (int)(b == c));
        std::weak_ptr<Base> w = b;
        std::printf("weak %ld %d\n", w.use_count(), (int)w.expired());
        b.reset();
        std::printf("%ld %d\n", c.use_count(), (int)(b == nullptr));
        c.reset();
        std::printf("weak %ld %d %d\n", w.use_count(), (int)w.expired(), (int)(w.lock() == nullptr));
        try {
            std::shared_ptr<Base> dead(w);
        } catch (const std::bad_weak_ptr &e) {
            std::printf("caught %s\n", e.what());
        }
    }
    {
        std::shared_ptr<int> t(new int(5), Tell());
        std::shared_ptr<int> u = t;
        t.reset(new int(6), Tell());
        std::printf("%d %d\n", *t, *u);
    }
    {
        std::shared_ptr<int> arr(new int[3], std::default_delete<int[]>());
        arr.get()[2] = 9;
        std::printf("%d\n", arr.get()[2]);
    }
    {
        std::shared_ptr<Pair> p(new Pair(3, 4));
        std::shared_ptr<int> alias(p, &p->b);
        p.reset();
        std::printf("alias %d %ld\n", *alias, alias.use_count());
    }
    std::shared_ptr<int> empty;
    std::printf("%ld %d %d\n", empty.use_count(), (int)(empty == nullptr), (int)(bool)empty);
    return 0;
}
