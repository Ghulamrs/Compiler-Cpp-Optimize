// [temp.inst]/1: the member class is used, so its definition is instantiated, and T::nothing does not exist.
template <class T> struct Box { struct Bad { typename T::nothing n; }; };
int main() { Box<int>::Bad b; (void)b; return 0; }
