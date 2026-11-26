#include <cstddef>
#include <cstdio>
#include <memory>
#include <new>
#include <string>
#include <utility>
struct Tracked {
    int v;
    static int live;
    Tracked(int v) : v(v) { live++; }
    Tracked(const Tracked &o) : v(o.v) {
        if (o.v == 13) throw 13;
        live++;
    }
    ~Tracked() { live--; }
};
int Tracked::live = 0;
struct Odd { int x; int *operator&() { return 0; } };
struct WithAlloc { typedef std::allocator<int> allocator_type; };
struct MinimalAlloc {
    typedef int value_type;
    int *allocate(std::size_t n) { return static_cast<int *>(operator new(n * sizeof(int))); }
    void deallocate(int *p, std::size_t) { operator delete(p); }
    std::size_t max_size() const { return 1000; }
};
int main() {
    std::allocator<std::string> a;
    std::string *s = a.allocate(2);
    typedef std::allocator_traits<std::allocator<std::string> > traits;
    new (static_cast<void *>(s)) std::string("built");
    new (static_cast<void *>(s + 1)) std::string(3, 'z');
    std::printf("%s %s\n", s[0].c_str(), s[1].c_str());
    typedef std::string S;
    s[0].~S();
    s[1].~S();
    std::printf("%d\n", (int)(traits::max_size(a) == a.max_size()));
    traits::deallocate(a, s, 2);
    typedef std::allocator_traits<MinimalAlloc> mtraits;
    MinimalAlloc m;
    mtraits::pointer mp = mtraits::allocate(m, 3);
    *mp = 41;
    std::printf("%d %d %d\n", *mp + 1, (int)mtraits::max_size(m), (int)sizeof(mtraits::difference_type));
    mtraits::deallocate(m, mp, 3);
    std::printf("%d %d\n", (int)std::uses_allocator<WithAlloc, std::allocator<int> >::value, (int)std::uses_allocator<int, std::allocator<int> >::value);
    int arr[3] = {1, 2, 3};
    std::printf("%d\n", *std::pointer_traits<int *>::pointer_to(arr[1]));
    Tracked src[4] = {Tracked(1), Tracked(2), Tracked(3), Tracked(4)};
    void *raw = operator new(sizeof(Tracked) * 4);
    Tracked *dst = static_cast<Tracked *>(raw);
    Tracked *end = std::uninitialized_copy(src, src + 4, dst);
    std::printf("copy %d %d %d\n", (int)(end - dst), dst[3].v, Tracked::live);
    for (Tracked *p = dst; p != end; ++p) p->~Tracked();
    end = std::uninitialized_copy_n(src + 1, 2, dst);
    std::printf("copy_n %d %d %d\n", (int)(end - dst), dst[1].v, Tracked::live);
    for (Tracked *p = dst; p != end; ++p) p->~Tracked();
    std::uninitialized_fill(dst, dst + 4, Tracked(9));
    std::printf("fill %d %d\n", dst[2].v, Tracked::live);
    for (int i = 0; i < 4; i++) dst[i].~Tracked();
    std::uninitialized_fill_n(dst, 3, Tracked(8));
    std::printf("fill_n %d %d\n", dst[2].v, Tracked::live);
    for (int i = 0; i < 3; i++) dst[i].~Tracked();
    std::raw_storage_iterator<Tracked *, Tracked> out(dst);
    *out++ = Tracked(5);
    *out = Tracked(6);
    std::printf("raw %d %d %d\n", dst[0].v, dst[1].v, Tracked::live);
    dst[0].~Tracked();
    dst[1].~Tracked();
    operator delete(raw);
    std::pair<int *, std::ptrdiff_t> buf = std::get_temporary_buffer<int>(16);
    std::printf("buffer %d\n", (int)(buf.first != 0 && buf.second == 16));
    std::return_temporary_buffer(buf.first);
    Odd o;
    o.x = 4;
    std::printf("addressof %d %d\n", (int)(&o == 0), std::addressof(o)->x);
    char space[64];
    void *p = space + 1;
    std::size_t room = sizeof space - 1;
    void *aligned = std::align(16, 8, p, room);
    std::printf("align %d %d %d\n", (int)(aligned != 0), (int)((std::size_t)aligned % 16), (int)(room + ((char *)p - space) == sizeof space));
    room = 4;
    p = space + 1;
    std::printf("align %d\n", (int)(std::align(16, 8, p, room) == 0));
    int *leak = new int(1);
    std::declare_reachable(leak);
    int *back = std::undeclare_reachable(leak);
    std::printf("%d %d\n", (int)(back == leak), (int)(std::get_pointer_safety() == std::pointer_safety::relaxed));
    delete back;
    std::auto_ptr<Tracked> ap(new Tracked(21));
    std::auto_ptr<Tracked> ap2(ap);
    std::printf("auto_ptr %d %d %d\n", (int)(ap.get() == 0), ap2->v, Tracked::live);
    ap = ap2;
    ap.reset();
    std::printf("auto_ptr %d\n", Tracked::live);
    std::allocator_arg_t tag = std::allocator_arg;
    (void)tag;
    return 0;
}
