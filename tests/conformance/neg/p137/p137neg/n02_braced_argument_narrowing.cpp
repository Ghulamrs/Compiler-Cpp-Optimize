// [dcl.init.list]/7: a braced argument for an int parameter may not narrow a double.
int f(int) { return 1; }
int f(const char *) { return 2; }
int main() { double d = 1.5; return f({d}); }