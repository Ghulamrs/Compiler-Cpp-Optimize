// [temp.mem] and [basic.stc.dynamic]/2: a static member template through a template-id, ::operator new and delete by name.
#include <cstdio>
#include <new>
template <class A> struct Traits {
    template <class T, class U> static void construct(A &, T *p, const U &u) { ::new (static_cast<void *>(p)) T(u); }
    template <class T> static void destroy(A &, T *p) { p->~T(); }
};
struct Alloc {};
struct V { int v; V(int x) : v(x) {} ~V() { std::printf("~V %d\n", v); } };
int main() {
    Alloc a;
    void *raw = ::operator new(sizeof(V));
    V *p = static_cast<V *>(raw);
    Traits<Alloc>::construct(a, p, 7);
    std::printf("%d\n", p->v);
    Traits<Alloc>::destroy(a, p);
    ::operator delete(raw);
    return 0;
}
