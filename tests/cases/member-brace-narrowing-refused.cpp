// [dcl.init.list]/7: a braced member initialiser may not narrow. clang: "constant expression
// evaluates to 300 which cannot be narrowed to type 'char'" (3:21).
struct S { char c{300}; S() {} };
int main() { S s; return s.c; }
