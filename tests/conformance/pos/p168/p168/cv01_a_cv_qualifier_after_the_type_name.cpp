// [dcl.type]/1: the decl-specifiers of a declaration come in any order, so `const` and `volatile` may follow the
// type's name - a class, a typedef, a qualified name, a template parameter - as they follow `int`. 1.66 and every
// release before refused them after any type but a built-in one with "expected a name": `T const &`,
// `std::size_t const n`, `S volatile s`. 1.67 takes them, before and after the name alike.
#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

typedef int I;
struct S { int v; S() : v(7) {} int get() const { return v; } };
typedef S T;
struct A { typedef long L; };
namespace n { struct P { int x; }; }

I const ni = 3;                              // at namespace scope, a typedef
std::size_t const count = 4;                 // a qualified typedef from the library
A::L const nested = 5;                       // a member typedef
S const sc;                                  // a class
T const tc;                                  // a typedef of a class
n::P const np = { 6 };                       // a qualified class
I const *pi = &ni;                           // then a declarator with a pointer
I const &ri = ni;                            // and with a reference
S volatile sv;                               // volatile, the same way

template <class U> U const &larger(U const &a, U const &b) { return a < b ? b : a; }
template <class U> struct Box {
    U const item;                            // a member whose type is a template parameter
    explicit Box(U const &u) : item(u) {}
    U const &get() const { return item; }
};

int sum(std::vector<int> const &v) {         // a parameter of a class template's specialisation
    int s = 0;
    for (std::vector<int>::const_iterator it = v.begin(); it != v.end(); ++it) s += *it;
    return s;
}

int lengthOf(std::string const &s) { return static_cast<int>(s.size()); }

int main() {
    I const local = 9;                       // in a block
    T const *pt = &tc;
    std::vector<int> v;
    v.push_back(1); v.push_back(2); v.push_back(3);
    Box<std::string> const box(std::string("four"));
    std::printf("%d %d %ld %d %d %d\n", ni, static_cast<int>(count), nested, sc.get(), tc.get(), np.x);
    std::printf("%d %d %d %d\n", *pi, ri, sv.v, local);
    std::printf("%d %d %d\n", larger(3, 8), sum(v), lengthOf(box.get()));
    std::printf("%d\n", pt->get());
    return 0;
}
