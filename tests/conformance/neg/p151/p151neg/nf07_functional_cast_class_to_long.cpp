// [expr.type.conv]/1: `T(x)` is `(T)x` - a class with no conversion function reaches no scalar by it.
struct Q { int v; };
int main() { Q q = {3}; long x = long(q); return (int)x; }
