// A16: the primary's out-of-line static member and member function belong to
// the primary's instantiations; one made from a partial specialization has
// its own members and replays neither.
extern "C" int printf(const char *, ...);
template <class T> struct Box { T v; Box(T x) : v(x) {} T get() const; static int count; };
template <class T> int Box<T>::count = 0;
template <class T> T Box<T>::get() const { return v; }
template <class T> struct Box<T *> { T *p; Box(T *x) : p(x) {} T get() const { return *p + 1; } };
int main() {
    Box<int> bi(4); int z = 9; Box<int *> bp(&z);
    typedef Box<int> BI; BI::count = 3;
    printf("%d %d %d\n", bi.get(), bp.get(), BI::count);
    return 0;
}
