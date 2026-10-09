// [dcl.typedef]/6: a typedef-name may be redeclared only to the type it already names. clang:
// "typedef redefinition with different types ('long' vs 'int')" (4:7).
using X = int;
using X = long;
int main() { X a = 0; return (int)a; }
