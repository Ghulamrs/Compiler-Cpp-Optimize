#include <cstdio>

// A base of a virtual base, reached from a class derived from the one that names the virtual base: the derived
// class lays the virtual base out after its own members, so the base is found through the vbtable or vtable.
class Root {
public:
    long long width() const { return width_; }
    unsigned flags() const { return flags_; }
protected:
    Root() : width_(0), flags_(0x1234) {}
    long long width_;
    unsigned flags_;
};
class Shared : public Root {
protected:
    Shared() : fill_(' ') {}
    char fill_;
};
class Stream : virtual public Shared {
public:
    explicit Stream(int) {}
    virtual ~Stream() {}
protected:
    Stream() {}
};
class StringStream : public Stream {
    int buffer_[4];
public:
    StringStream() : Stream(1) { buffer_[0] = 9; }
};
template <class C> class TStream : virtual public Shared {
public:
    virtual ~TStream() {}
};
template <class C> class TStringStream : public TStream<C> {
    C text_[24];
public:
    TStringStream() { text_[0] = 0; }
};

static long long viaPointer(const Root *r) { return r->width() + r->flags(); }

int main() {
    StringStream s;
    TStringStream<char> t;
    std::printf("%lld %x %lld %x %lld %lld\n", s.width(), s.flags(), t.width(), t.flags(), viaPointer(&s), viaPointer(&t));
    return 0;
}
