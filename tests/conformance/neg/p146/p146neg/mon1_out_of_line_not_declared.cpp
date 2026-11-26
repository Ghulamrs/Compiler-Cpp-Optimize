// [class.mfct]/2: a member template defined outside its class must be declared in it; S declares no conv.
struct S { template <class T> T other(T) const; };
template <class T> T S::conv(T t) const { return t; }
int main() { return 0; }
