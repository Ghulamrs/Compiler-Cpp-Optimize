// [temp.res]/10: a member template of an ordinary class, never used, calling a function declared nowhere.
struct C { template <class T> int m(T) { return absent(2); } };
int main() { C c; (void)c; return 0; }
