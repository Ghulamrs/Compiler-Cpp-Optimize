// [dcl.type]/2: `volatile` written twice among one declaration's specifiers is ill-formed. A typedef that holds a
// `volatile` already is not a second one (that one is in its type); two written are.
typedef int I;
I volatile volatile v;
int main() { return 0; }
