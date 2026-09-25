// A static local array of a class: built once, under the guard one object
// gets, and destroyed at exit.
extern "C" int printf(const char *, ...);
static int built = 0;
struct S { int v; S() : v(built++) {} ~S() { printf("~%d ", v); } };
int f(int k) { static S arr[2]; return arr[k].v + built; }
int main() { printf("%d %d %d\n", f(0), f(1), built); return 0; }
