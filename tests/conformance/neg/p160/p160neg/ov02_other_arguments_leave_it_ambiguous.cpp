// [over.match.best]: when neither candidate is better on the other arguments, the call is ambiguous.
int f(int x) { return x; }
double f(double x) { return x; }
int g(int (*fn)(int), long v) { return fn((int)v); }
int g(double (*fn)(double), unsigned v) { return (int)fn(v); }
int main() { return g(f, 4); }
