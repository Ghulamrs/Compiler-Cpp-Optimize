// [allocator.adaptor.types] and [allocator.adaptor.members]: the outer and inner allocators, rebind, the propagation
// traits, comparison and select_on_container_copy_construction, with one inner allocator and with none.
#include <cstdio>
#include <memory>
#include <scoped_allocator>
#include <type_traits>
template <class T> struct Al {
    typedef T value_type;
    typedef std::true_type propagate_on_container_swap;
    int id;
    explicit Al(int i = 0) : id(i) {}
    template <class U> Al(const Al<U> &o) : id(o.id) {}
    T *allocate(std::size_t n) { return static_cast<T *>(::operator new(n * sizeof(T))); }
    void deallocate(T *p, std::size_t) { ::operator delete(p); }
};
template <class T, class U> bool operator==(const Al<T> &a, const Al<U> &b) { return a.id == b.id; }
template <class T, class U> bool operator!=(const Al<T> &a, const Al<U> &b) { return a.id != b.id; }
typedef std::scoped_allocator_adaptor<Al<int> > One;
typedef std::scoped_allocator_adaptor<Al<int>, Al<char> > Two;
int main() {
    One a(Al<int>(3));
    Two b(Al<int>(4), Al<char>(5));
    One c(a);
    std::printf("%d %d %d %d\n", a.outer_allocator().id, a.inner_allocator().id, b.outer_allocator().id, b.inner_allocator().id);
    std::printf("%d %d %d\n", (int)(a == c), (int)(a != One(Al<int>(9))), (int)std::is_same<One::rebind<long>::other, std::scoped_allocator_adaptor<Al<long> > >::value);
    std::printf("%d %d\n", (int)One::propagate_on_container_swap::value, (int)One::propagate_on_container_copy_assignment::value);
    int *p = a.allocate(2);
    a.construct(p, 7);
    std::printf("%d %d\n", *p, a.select_on_container_copy_construction().id);
    a.destroy(p);
    a.deallocate(p, 2);
    return 0;
}
