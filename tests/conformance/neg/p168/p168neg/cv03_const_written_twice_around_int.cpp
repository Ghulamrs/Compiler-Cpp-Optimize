// [dcl.type]/2: `const` written twice among one declaration's specifiers is ill-formed, one before `int` and one
// after it. 1.66 took the second as if it were not there.
const int const a = 1;
int main() { return a - 1; }
