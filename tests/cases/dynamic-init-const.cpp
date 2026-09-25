// A const object at file scope may still be initialised at run time - it is
// then not a constant expression, and it lies in writable data.
extern "C" int printf(const char *, ...);
int init(int v) { return v; }
const int k = init(4);
const double d = k * 0.5;
struct W { static const int m; };
const int W::m = init(6);
int main() { printf("%d %.1f %d\n", k, d, W::m); return 0; }
