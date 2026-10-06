// [class.static.data]/2: the initialiser in the definition of a static data
// member is in the scope of its class - a name in it is looked up as in a
// member function of that class - and so is an array bound written after the
// declarator-id. cxx1 read both at namespace scope: `int D::modes_[D::Count] =
// { Text, Text };` was "'Text' was not declared", and where a global of the
// same name existed it was found instead of the member.
//
// Every shape below is one RTS6x or a reader would write: enumerators, a
// static const, a nested type and typedef, a base's member, a static member
// function, private members, a class in a namespace defined outside it, a
// constructor's arguments, and a class template's member, scalar and array.
extern "C" int printf(const char *, ...);

int Count = 99;           // hidden by the class's own in every definition below
const int Text = 7;

struct Base { enum { BaseVal = 11 }; static const int bs = 5; };

class D : public Base {
public:
    enum { Text = 0x4000, Count = 4 };
    struct Nested { int a, b, c; };
    typedef short Small;
    static const int sc = 3;
    static int modes_[Count];
    static int k;
    static int arr2[Count];
    static int fromBase;
    static int size;
    static Small small;
    static int product;
    static int helper() { return 6; }
    static int fromFn;
};
int D::modes_[D::Count] = { Text, Text };
int D::arr2[Count] = { Count, sc, BaseVal, bs };
int D::k = Count;
int D::fromBase = BaseVal + bs;
int D::size = sizeof(Nested);
D::Small D::small = sizeof(Small) + Text;
int D::product = sc * 10;
int D::fromFn = helper();

struct Box { int v; Box(int x); };
Box::Box(int x) : v(x) {}     // out of line, so clang emits C1 as cxx1 does
class P {
    enum { N = 3, Seed = 40 };
    static const int scale = 2;
    static int a[N];
    static Box b;
public:
    static int get(int i) { return a[i]; }
    static int box() { return b.v; }
};
int P::a[N] = { N, Seed, scale };
Box P::b(Seed + scale);

namespace n { struct C { enum { Count = 8 }; static int k; static int a[Count]; }; }
int n::C::k = Count;
int n::C::a[Count] = { Count };

template <class T> struct TC { enum { Count = 5 }; static int arr[Count]; static T t; };
template <class T> int TC<T>::arr[Count] = { Count, Count + 1 };
template <class T> T TC<T>::t = Count * 2;

int main() {
    printf("%d %d %d %d\n", D::modes_[0], D::modes_[1], D::modes_[2], D::k);
    printf("%d %d %d %d %d\n", D::arr2[0], D::arr2[1], D::arr2[2], D::arr2[3],
           (int)sizeof D::arr2);
    printf("%d %d %d %d %d\n", D::fromBase, D::size, (int)D::small, D::product,
           D::fromFn);
    printf("%d %d %d %d\n", P::get(0), P::get(1), P::get(2), P::box());
    printf("%d %d %d\n", n::C::k, n::C::a[0], (int)sizeof n::C::a);
    printf("%d %d %d %d\n", TC<int>::arr[0], TC<int>::arr[1],
           (int)sizeof TC<int>::arr, TC<double>::arr[0]);
    printf("%d %g\n", TC<int>::t, TC<double>::t);
    const int *text = &Text;      // an address, so both compilers emit the const
    printf("%d %d\n", Count, *text);
    return 0;
}
