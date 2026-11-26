// [dcl.spec.auto]/2 in C++11: a function declared with 'auto' needs a trailing return type - deduction is C++14.
struct S { static auto f() { return 1; } };
int main() { return S::f(); }
