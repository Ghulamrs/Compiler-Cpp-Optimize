// The library session's item 36: shared_ptr, weak_ptr and exception_ptr in one translation unit (a compiler crash at 1.42).
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
    {
        std::unique_ptr<Derived> up(new Derived(2));
        std::shared_ptr<Base> sp(std::move(up));
        std::printf("%d %d\n", (int)(up.get() == 0), sp->b);
        std::shared_ptr<Derived> sd(new Derived(3));
        std::shared_ptr<Base> sb(sd);
        std::shared_ptr<Base> moved(std::move(sd));
        std::printf("%ld %d\n", sb.use_count(), (int)(sd.get() == 0));
    }
    {
        Node *raw = new Node(7);
        std::shared_ptr<Node> n(raw);
        std::shared_ptr<Node> again = raw->shared_from_this();
        std::printf("%ld %d\n", n.use_count(), (int)(again == n));
        const Node *cn = raw;
        std::shared_ptr<const Node> cagain = cn->shared_from_this();
        std::printf("%ld %d\n", n.use_count(), cagain->v);
    }
    {
        std::shared_ptr<int> a(new int(1)), b(new int(2));
        std::shared_ptr<int> a2 = a;
        std::weak_ptr<int> wa = a;
        std::printf("%d %d %d\n", (int)(!a.owner_before(a2) && !a2.owner_before(a)), (int)(a.owner_before(b) != b.owner_before(a)),
            (int)(!wa.owner_before(a) && !a.owner_before(wa)));
        std::owner_less<std::weak_ptr<int> > wless;
        std::owner_less<std::shared_ptr<int> > sless;
        std::weak_ptr<int> wb = b;
        std::printf("owner_less %d %d %d\n", (int)(wless(wa, wb) != wless(wb, wa)), (int)wless(wa, std::weak_ptr<int>(a2)),
            (int)(sless(a, b) == wless(wa, wb)));
        std::shared_ptr<int> loaded = std::atomic_load(&a);
        std::atomic_store(&loaded, b);
        std::printf("atomic %d %ld\n", *loaded, b.use_count());
        std::shared_ptr<int> expect = a;
        std::printf("cas %d", (int)std::atomic_compare_exchange_strong(&loaded, &expect, a));
        std::printf(" %d %d\n", *expect, (int)std::atomic_is_lock_free(&a));
        a.swap(b);
        std::printf("%d %d\n", *a, *b);
    }
    std::shared_ptr<int> empty;
    std::printf("%ld %d %d\n", empty.use_count(), (int)(empty == nullptr), (int)(bool)empty);
    return 0;
}
