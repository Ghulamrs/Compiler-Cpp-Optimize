#include <iostream>

// [ostream]/1: basic_ostream's copy constructor is deleted.
int main() {
    std::ostream copy = std::cout;
    copy << 1;
    return 0;
}
