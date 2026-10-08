// A template template parameter, [temp.param]/1: C stands for a template, not
// a type, which is a third kind of binding beside the two this parser has -
// a type name and an enumerator - and the Itanium name would spell it TT_.
// Refused by name at the parameter. clang -std=c++11 -pedantic-errors accepts
// this program and it returns 0.
template <class T> struct Box { T v; };
template <template <class> class C> struct Holder { C<int> c; };
int main() { Holder<Box> h; h.c.v = 3; return h.c.v - 3; }
