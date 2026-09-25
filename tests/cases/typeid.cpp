// **`typeid`, static and dynamic** - [expr.typeid]: a type-id or a
// non-polymorphic operand names the static type's object, unevaluated; a
// glvalue of polymorphic class reads its own through the vptr, evaluated.
// std::type_info is declared here as <typeinfo> declares it - the mangling
// oracle cross-compiles with no headers - and reads the object as the ABI
// lays it out. Refused by name until 2026-09-14 (review A5).
extern "C" int strcmp(const char *, const char *);
namespace std {
class type_info {
public:
    const char *name() const { return __name; }
    bool operator==(const type_info &o) const { return __name == o.__name || strcmp(__name, o.__name) == 0; }
    bool operator!=(const type_info &o) const { return !(*this == o); }
    bool before(const type_info &o) const { return strcmp(__name, o.__name) < 0; }
    unsigned long hash_code() const {
        unsigned long h = 5381;
        for (const char *p = __name; *p != 0; p++) h = h * 33 + (unsigned char)*p;
        return h;
    }
private:
    const void *__vptr;
    const char *__name;
};
}
extern "C" int printf(const char *, ...);
struct B { virtual ~B() {} int b; };
struct D : B { int d; };
struct Plain { int p; };
enum Colour : unsigned char { Red, Green };
enum Plainer { Zero };   // an enumerator of an int enum is an int here (CONFORMANCE.md)
struct Tmp { ~Tmp() { printf("~Tmp "); } };
B *pick(const Tmp &, B *b) { return b; }
int side = 0;
B *make(int k) { side++; if (k) return new D; return new B; }
int main() {
    B bobj; D dobj;
    B &ref = dobj;
    B *pb = &dobj;
    const std::type_info &ti = typeid(dobj);
    printf("%d %d %d %d\n", typeid(int) == typeid(int), typeid(int) == typeid(unsigned), typeid(dobj) == typeid(D), typeid(bobj) == typeid(D));
    printf("%d %d %d %d\n", typeid(*pb) == typeid(D), typeid(ref) == typeid(D), typeid(pb) == typeid(B *), typeid(*pb) != typeid(B));
    printf("%d %d %d %d\n", ti == typeid(D), typeid(Plain) == typeid(Plain), typeid(char) != typeid(signed char), typeid(const int) == typeid(int));
    printf("%s %s %s %s\n", typeid(int).name(), typeid(D).name(), typeid(*pb).name(), typeid(const char *).name());
    printf("%d %d %d\n", typeid(*make(1)) == typeid(D), typeid(make(0)) == typeid(B *), side);
    printf("%d %d\n", typeid(double).before(typeid(int)) != typeid(int).before(typeid(double)), typeid(D).hash_code() == typeid(dobj).hash_code());
    Colour c = Green;
    printf("%d %d %s %s\n", typeid(c) == typeid(Colour), typeid(Colour) == typeid(Plainer), typeid(Colour).name(), typeid(Plainer).name());
    printf("%s ", typeid(*pick(Tmp(), &dobj)).name());
    printf("| %d\n", typeid(pick(Tmp(), &dobj)) == typeid(B *));
    return 0;
}
