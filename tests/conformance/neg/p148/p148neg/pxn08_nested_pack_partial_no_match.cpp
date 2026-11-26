// [temp.class.spec.match]/2: Tup<H, T...> needs at least one member, so Tup<> matches no specialization and the primary is undefined.
template <class... Ts> struct Tup {};
template <class Tp> struct Head;
template <class H, class... T> struct Head<Tup<H, T...> > { typedef H type; };
int main() { Head<Tup<> >::type x = 0; return x; }
