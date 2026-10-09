// `using X = T;` - [dcl.typedef]/2: an alias declaration names a type exactly as `typedef T X;`
// does, in whichever scope it stands - file scope, a namespace, a class body, a block. Every
// shape a typedef takes: a pointer, a reference, an array, a function type, a function pointer,
// a template-id, an alias of an alias, and a redeclaration to the same type. clang agrees.
extern "C" int printf(const char *, ...);

using Int = int;
using Int = int;
using IntPtr = Int *;
using IntRef = const Int &;
using Three = int[3];
using Fn = int(int);
using FnPtr = int (*)(int, int);

template <class T> struct Box { T v; T get() const { return v; } };
using IntBox = Box<Int>;

namespace lib {
    using Count = unsigned long;
    Count twice(Count n) { return n * 2; }
}

struct S {
    using Value = long long;
    using Self = S;
    Value v;
    Self *me() { return this; }
    static Value sum(Value a, Value b) { return a + b; }
};

int add(int a, int b) { return a + b; }
int square(int n) { return n * n; }

int main() {
    Int n = 5;
    IntPtr p = &n;
    IntRef r = n;
    Three arr = { 1, 2, 3 };
    Fn *f = square;
    FnPtr g = add;
    IntBox box;
    box.v = 7;
    using Local = double;
    Local d = 2.5;
    S s;
    s.v = 40;
    S::Value total = S::sum(s.v, 2);
    lib::Count c = lib::twice(21);
    printf("%d %d %d\n", *p, r, arr[2]);
    int fv = f(6);
    int gv = g(2, 3);
    printf("%d %d %d\n", fv, gv, box.get());
    printf("%.1f %lld %lld %lu\n", d, total, s.me()->v, c);
    printf("%d %d %d\n", (int)sizeof(Three), (int)sizeof(IntPtr) == (int)sizeof(int *), (int)sizeof(S::Value));
    return 0;
}
