// [temp.deduct.type]/10: a function type's trailing parameter pack deduces from the parameters an argument's type leaves.
#include <cstdio>
template <class... Ts> int arity(int (*)(Ts...)) { return (int)sizeof...(Ts); }
template <class R, class... Ts> int arityR(R (*)(Ts...)) { return (int)sizeof...(Ts) * 10 + (int)sizeof(R); }
template <class First, class... Rest> int afterFirst(void (*)(First, Rest...)) { return (int)sizeof(First) * 10 + (int)sizeof...(Rest); }
template <class... Ts> int sum(int (*f)(Ts...), Ts... ts) { return f(ts...); }
int none() { return 0; }
int two(int a, int b) { return a + b; }
int three(int a, char b, double c) { return a + b + (int)c; }
long long wide(short) { return 0; }
void takes(double, int, int) {}
void one(char) {}
int main() {
    std::printf("%d %d %d\n", arity(none), arity(two), arity(three));
    std::printf("%d %d\n", arityR(wide), arityR(three));
    std::printf("%d %d\n", afterFirst(takes), afterFirst(one));
    std::printf("%d %d\n", sum(two, 3, 4), sum(three, 1, 'a', 2.5));
    return 0;
}
