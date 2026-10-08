// [class]/3: a class marked `final` may not be a base. clang: "base 'Leaf' is
// marked 'final'".
struct Leaf final { int v; };
struct More : Leaf { int w; };
int main() { More m; m.v = 1; m.w = 2; return m.v + m.w; }
