// an error inside a template, three instantiations deep
template <class T> struct Inner { typename T::missing value; };
template <class T> struct Middle { Inner<T> in; };
template <class T> void outer() { Middle<T> m; (void)m; }
int main() { outer<int>(); return 0; }
