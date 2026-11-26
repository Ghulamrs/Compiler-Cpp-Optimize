// [temp.res]/10: a member function of a class template never used, naming nothing declared.
template <class T> struct S { static int get() { return Nowhere::value; } };
int main() { return 0; }
