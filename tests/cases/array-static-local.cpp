// A static local array of a class: built once, under the guard one object
// gets, and destroyed at exit.
extern "C" int printf(const char *, ...);
static int built = 0;
struct S { int v; S() : v(built++) {} ~S() { printf("~%d ", v); } };
int f(int k) { static S arr[2]; return arr[k].v + built; }
int main() {
    int a = f(0);
    int b = f(1);
    printf("%d %d %d\n", a, b, built);
    return 0;
}
