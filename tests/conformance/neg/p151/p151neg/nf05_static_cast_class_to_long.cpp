// [expr.static.cast]/4: a class with no conversion function reaches no scalar.
struct Q { int v; };
int main() { Q q = {3}; long x = static_cast<long>(q); return (int)x; }
