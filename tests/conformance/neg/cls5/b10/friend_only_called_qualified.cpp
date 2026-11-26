// [namespace.memdef]/3: qualified lookup does not find a name only a friend declaration has introduced.
namespace n {
    struct A {
        friend int seven(int);
    };
}
int main() {
    return n::seven(1);
}
