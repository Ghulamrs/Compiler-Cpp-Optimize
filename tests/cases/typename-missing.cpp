// A27: cl's C7510 - the dependent name is read as a type without 'typename'.
struct T { typedef int type; };
template <class X> struct U { X::type v; };
int main() { U<T> u; u.v = 3; return u.v; }
