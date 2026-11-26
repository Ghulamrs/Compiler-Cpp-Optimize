// [over.match.oper]/3 with [temp.over]/1: operator==<int>, made by x == y, is no candidate of c == x - deduction
// from C fails, and a conversion to V<int> is never tried.
template <class T> struct V { T v; };
template <class T> bool operator==(V<T>, V<T>) { return true; }
struct C { operator V<int>() const { V<int> r = {1}; return r; } };
int main() { V<int> x = {1}, y = {2}; bool a = x == y; C c; bool b = c == x; return a && b; }
