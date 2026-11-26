// [expr.cast]/4: a cast notation is one of the named casts or none - a class with no conversion function
// reaches no scalar by it.
struct Q { int v; };
int main() { Q q = {3}; long x = (long)q; return (int)x; }
