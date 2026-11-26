// [over.over]/1: neither h converts to a pointer to a function taking a char *.
int h(int) { return 1; }
int h(double) { return 2; }
struct Q { int (*hc)(char *); };
int main() { Q q = { h }; return q.hc(0); }