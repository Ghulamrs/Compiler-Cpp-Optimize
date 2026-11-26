#include <sstream>

// [istringstream]/1: basic_istringstream's copy assignment is deleted.
int main() {
    std::istringstream a("1"), b("2");
    a = b;
    return 0;
}
