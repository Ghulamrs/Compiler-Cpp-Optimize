// [dcl.type]/2: `const` written twice among one declaration's specifiers is ill-formed, after a class's name as
// before it. 1.66 read no `const` after a class's name at all; 1.67 reads it and refuses the second.
struct S {};
S const const s = S();
int main() { return 0; }
