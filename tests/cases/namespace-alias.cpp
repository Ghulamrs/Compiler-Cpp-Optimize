// A namespace alias - [namespace.alias]: `namespace A = N;` makes A another
// name for N, for every use a namespace name has here: a qualifier in an
// expression, in a type, in a member definition, in a using-directive, as the
// target of a second alias, and as `N::A` from outside the namespace it is
// written in.
extern "C" int printf(const char *, ...);

namespace library {
    int version = 3;
    int twice(int x) { return x * 2; }
    struct Point {
        int x, y;
        Point(int a, int b);
        int sum() const;
    };
    template <class T> struct Box { T held; T get() const { return held; } };
    namespace detail {
        int secret() { return 42; }
        enum Kind { Red = 7, Green };
    }
    namespace impl = detail;
    int viaInnerAlias() { return impl::secret() + impl::Green; }
}

namespace lib = library;
namespace deep = library::detail;
namespace again = lib;
namespace fromGlobal = ::library;

lib::Point::Point(int a, int b) : x(a), y(b) {}
int lib::Point::sum() const { return x + y; }

int useDirective() {
    using namespace lib;
    return twice(version);
}

int main(void) {
    printf("%d %d\n", lib::version, lib::twice(5));
    lib::Point p(2, 3);
    printf("%d\n", p.sum());
    deep::Kind k = deep::Red;
    printf("%d %d\n", k, deep::secret());
    again::Box<int> b;
    b.held = 9;
    printf("%d %d\n", b.get(), fromGlobal::version);
    printf("%d\n", useDirective());
    printf("%d %d\n", library::impl::secret(), lib::viaInnerAlias());
    lib::version = 11;
    printf("%d\n", library::version);
    return 0;
}
