#include <ios>

// [ios.base.cons]/1: ios_base's constructor is protected.
int main() {
    std::ios_base b;
    return b.width() == 0 ? 0 : 1;
}
