template <class T> struct X { static int s; };
template <class T = int> int X<T>::s = 0;
int main() { return 0; }
