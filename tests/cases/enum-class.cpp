// `enum class` - [dcl.enum]/2, /10: a type of its own whose enumerators are reached through it,
// with a fixed underlying type (int unless an enum-base says otherwise), and no implicit
// conversion to an integer or to bool; a cast reaches the integer, two of one type compare,
// and it governs a switch. C++11 also lets an unscoped enumerator be named `E::a`. An opaque
// declaration, `enum class E;` or `enum E : int;`, declares the type before its body. Every
// line is clang's.
extern "C" int printf(const char *, ...);

enum class Colour { Red, Green = 5, Blue };
enum class Small : unsigned char { A = 1, B = 200 };
enum struct Dir : short { Up = -1, Down = 1 };
enum Plain { One = 1, Two };
enum class Later : long long;
enum Opaque : int;
enum class Later : long long { Far = 0x100000000LL };
enum Opaque : int { Shown = 9 };

// A scoped enumerator does not leak: these are ordinary names here.
int Red = 100;
const int Up = 7;

namespace n { enum class Mode { Off, On }; }
struct C {
    enum class Kind { Leaf = 3, Node };
    Kind k;
    int weight() const { return k == Kind::Node ? 2 : 1; }
};

// Inside the braces an enumerator is named plainly and has the underlying type.
enum class Seq { First = 2, Second = First * 3, Third };

int pick(Colour c) {
    switch (c) {
    case Colour::Red: return 1;
    case Colour::Green: return 2;
    default: return 3;
    }
}
// Through a template parameter, by a functional cast, as a truth value by a cast, and opaque
// behind a pointer before its body.
template <class T> int first() { return static_cast<int>(T::Green); }
int casts(Colour e) { return int(e) + (int)e + static_cast<int>(e); }
bool truth(Colour e) { return static_cast<bool>(e); }
enum class Fwd;
int peekFwd(const Fwd *p);
enum class Fwd { X = 11 };
int peekFwd(const Fwd *p) { return static_cast<int>(*p); }

int over(int) { return 10; }
int over(Colour) { return 20; }

int main() {
    Colour c = Colour::Blue;
    Colour d = Colour::Blue;
    printf("%d %d %d\n", static_cast<int>(c), (int)Colour::Green, (int)(c == d));
    printf("%d %d\n", (int)(Colour::Red < Colour::Blue), (int)(c != Colour::Red));
    printf("%d %d %d\n", (int)sizeof(Small), (int)Small::B, (int)sizeof(Dir));
    printf("%d %d %d\n", (int)Dir::Up, (int)Plain::Two, One);
    printf("%lld %d\n", (long long)Later::Far, (int)Opaque::Shown);
    printf("%d %d\n", Red, Up);
    n::Mode m = n::Mode::On;
    C x;
    x.k = C::Kind::Node;
    printf("%d %d %d\n", (int)m, x.weight(), (int)C::Kind::Leaf);
    printf("%d %d %d\n", (int)Seq::Second, (int)Seq::Third, pick(Colour::Green));
    int a = over(c);
    int b = over(3);
    printf("%d %d\n", a, b);
    Colour e = static_cast<Colour>(1);
    printf("%d\n", pick(e));
    int t = first<Colour>();
    int cs = casts(Colour::Green);
    int tr = truth(Colour::Red);
    Fwd fw = Fwd::X;
    int pf = peekFwd(&fw);
    printf("%d %d %d %d\n", t, cs, tr, pf);
    Colour arr[2] = { Colour::Red, Colour::Blue };
    printf("%d\n", (int)arr[1]);
    return 0;
}
