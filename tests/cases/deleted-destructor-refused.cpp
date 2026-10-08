// A deleted destructor is legal C++11 - [dcl.fct.def.delete] - and refused by name
// here: every place an object is destroyed would have to refuse it, and only calls
// are checked. clang accepts the declaration and refuses only `S s;`.
struct S { ~S() = delete; };
int main() { S *p = new S; (void)p; return 0; }
