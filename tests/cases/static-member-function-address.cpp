// The address of a static member function is an ordinary pointer to
// function - [expr.unary.op]/3 - and not a pointer to member.
extern "C" int printf(const char *, ...);
struct S {
    static int twice(int x) { return x * 2; }
    static int add(int a, int b) { return a + b; }
};
struct T : S { static int thrice(int x) { return x * 3; } };
int main() {
    int (*p)(int) = &S::twice;
    int (*q)(int, int) = &S::add;
    int (*r)(int) = &T::thrice;
    int a = p(4);
    int b = q(2, 5);
    int c = r(3);
    printf("%d %d %d\n", a, b, c);
    return 0;
}
