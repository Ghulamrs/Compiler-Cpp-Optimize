// [dcl.fct.def.delete]: a deleted function takes part in overload resolution and
// is refused where it wins - `f(2.5)` picks `f(double)` over `f(int)`, and that
// one is deleted. clang: "call to deleted function 'f'" at the call (6:14).
void f(int);
void f(double) = delete;
int main() { f(2.5); return 0; }
