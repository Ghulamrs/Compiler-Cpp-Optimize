// [temp.deduct.call]: T is written as int, and the call's char* cannot convert to it.
template <class T, class U> T pick(T t, U) { return t; }
int main() { char c = 0; return pick<int>(&c, 1); }
