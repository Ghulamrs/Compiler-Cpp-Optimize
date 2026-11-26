// R12: [temp.friend]/1 - a friend class template and a friend function template.
#include <cstdio>
template <class T> class H {
    int secret;
    template <class U> friend class H;
public:
    explicit H(int s) : secret(s) {}
    template <class U> int peek(const H<U> &o) const { return o.secret + secret; }
};
class B {
    int secret = 2;
    template <class T> friend int tpeek(const B &);
    template <class T> friend class Reader;
};
template <class T> int tpeek(const B &b) { return b.secret + (int)sizeof(T); }
template <class T> struct Reader { int read(const B &b) const { return b.secret * 10; } };
int main() {
    H<int> a(3); H<char> b(4);
    Reader<long> r;
    std::printf("%d %d %d %d\n", a.peek(b), b.peek(a), tpeek<double>(B()), r.read(B()));
    return 0;
}