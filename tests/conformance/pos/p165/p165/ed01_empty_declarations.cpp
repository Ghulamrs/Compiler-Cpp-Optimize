// [dcl.dcl]: `;` is an empty-declaration, at file scope, in a namespace and after a function's body.
;
int a;;
namespace N { ; int b; ; };
int main() { return 0; };
