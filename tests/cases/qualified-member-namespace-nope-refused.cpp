// A member a class does not have, named through its namespace, is answered about the class:
// clang says no member named 'nope' in 'n::S'. It used to say 'n' was not declared.
namespace n { namespace m { struct S { int x; }; } }
int main() { return sizeof(n::m::S::nope); }
