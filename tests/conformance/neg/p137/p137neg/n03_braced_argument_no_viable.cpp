// [over.ics.list]: two values initialise neither an int nor a pointer.
int f(int) { return 1; }
int f(const char *) { return 2; }
int main() { return f({1, 2}); }