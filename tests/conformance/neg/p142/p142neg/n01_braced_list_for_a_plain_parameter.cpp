// [temp.deduct.call]/1: a braced list given to `const T &` is a non-deduced context, and T has nothing else.
template <class T> int one(const T &) { return 1; }
int main() { return one({1, 2}); }