// [dcl.fct.def.delete]/2: naming a deleted function as a value is a use, as a call is.
// clang: "attempt to use a deleted function" (5:23).
void f(int) = delete;
int main() {
    void (*p)(int) = &f; (void)p;
    return 0;
}
