// [allocator.adaptor]: the inner allocator reaches each element a container makes, by uses-allocator construction, and
// both halves of a pair.
#include <cstdio>
#include <scoped_allocator>
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <utility>
static int tagged = 0;
template <class T> struct Tagged {
    typedef T value_type;
    int id;
    explicit Tagged(int i = 0) : id(i) {}
    template <class U> Tagged(const Tagged<U> &o) : id(o.id) {}
    T *allocate(std::size_t n) { ++tagged; return static_cast<T *>(::operator new(n * sizeof(T))); }
    void deallocate(T *p, std::size_t) { ::operator delete(p); }
};
template <class T, class U> bool operator==(const Tagged<T> &a, const Tagged<U> &b) { return a.id == b.id; }
template <class T, class U> bool operator!=(const Tagged<T> &a, const Tagged<U> &b) { return a.id != b.id; }
typedef std::basic_string<char, std::char_traits<char>, Tagged<char> > tstring;
typedef std::scoped_allocator_adaptor<Tagged<tstring> > outer;
int main() {
    outer a(Tagged<tstring>(7));
    std::vector<tstring, outer> v(a);
    v.push_back(tstring("a string long enough to need its own buffer, inner allocator please"));
    v.emplace_back("another string long enough to need its own buffer as well");
    v.emplace_back(3, 'x');
    std::printf("%d %d %d %d\n", (int)v.size(), v[0].get_allocator().id, v[1].get_allocator().id, v[2].get_allocator().id);
    std::printf("%d %d %d\n", (int)(v.get_allocator() == a), v.get_allocator().inner_allocator().id,
                (int)std::uses_allocator<tstring, Tagged<char> >::value);
    typedef std::scoped_allocator_adaptor<Tagged<std::pair<const int, tstring> > > mouter;
    std::map<int, tstring, std::less<int>, mouter> m(std::less<int>(), mouter(Tagged<std::pair<const int, tstring> >(9)));
    m.emplace(1, "mapped string long enough for a buffer of its own, inner one");
    m[2] = "short";
    std::printf("%d %d %d\n", (int)m.size(), m[1].get_allocator().id, (int)(tagged > 0));
    return 0;
}
