// [temp.res]/8: q is an int, and an int has no member called size - nothing here depends on T.
template <class T> int f(T) { int q = 0; return q.size(); }
int main() { return 0; }
