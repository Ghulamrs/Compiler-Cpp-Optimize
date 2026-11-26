#include <cstdio>
template <class T> struct B {
    unsigned v;
    class reference {

        B *b; int pos;

    public:
        reference(B &bb, int p) : b(&bb), pos(p) {}
        reference &operator=(bool x) { if (x) b->v |= 1u << pos; return *this; }
        reference &operator=(const reference &x) { b->v |= 64; return *this; }
    };
    B() : v(0) {}
    reference operator[](int p) { return reference(*this, p); }
};
int main() {
    B<int> nb;
    nb[2] = true;
    std::printf("%u\n", nb.v);
}
