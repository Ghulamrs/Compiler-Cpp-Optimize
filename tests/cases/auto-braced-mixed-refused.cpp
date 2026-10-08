// [temp.deduct.call]: every element of the list deduces the one E, and an
// `int` beside a `double` deduces two.
#include <initializer_list>
int main() {
    auto x = {1, 2.0};
    return static_cast<int>(x.size());
}
