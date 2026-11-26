// [over.match.oper]/3: N::R's operator!= is reached by no lookup for an N::S - argument-dependent lookup stops at N.
namespace N { struct S {}; namespace R { template <class T> bool operator!=(const T &, const T &) { return true; } } }
int main() { N::S a, b; return a != b; }
