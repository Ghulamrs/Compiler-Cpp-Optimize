// [namespace.memdef]/3: a friend defined in its class and never declared outside it, called with no arguments.
struct A {
    friend int zero() { return 0; }
};
int main() {
    return zero();
}
