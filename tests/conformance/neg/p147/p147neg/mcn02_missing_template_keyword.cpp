// [temp.names]/4: a member template of a dependent class named with arguments needs 'template'.
template <class T> struct Outer { template <class U> struct Inner { U u; }; };
template <class T> int use() { typename Outer<T>::Inner<int> in; in.u = 1; return in.u; }
int main() { return use<char>(); }
