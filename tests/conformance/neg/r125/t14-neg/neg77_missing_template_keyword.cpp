template <class T> struct O { template <class U> static U get() { return U(); } };
template <class T> int use() { return O<T>::get<int>(); }
int main() { return use<char>(); }
