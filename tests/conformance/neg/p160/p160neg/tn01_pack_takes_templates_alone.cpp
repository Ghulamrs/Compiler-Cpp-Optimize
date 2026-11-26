// [temp.arg.template]: a type is no argument for a template template parameter, in a pack or out of one.
template <class T> struct A {};
template <template <class> class... Ts> struct L {};
L<A, int> bad;
int main() { return 0; }
