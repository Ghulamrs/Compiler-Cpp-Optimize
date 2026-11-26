// [temp.deduct.type]/10: the parameters before the pack must match, and double is not int.
template <class... Ts> int f(int (*)(int, Ts...)) { return (int)sizeof...(Ts); }
int g(double, char) { return 0; }
int main() { return f(g); }
