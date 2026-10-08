// `auto x{1}` is std::initializer_list<int> in C++11's words and `int` after
// N3922, which compilers apply to C++11 as a defect report - refused by name.
#include <initializer_list>
int main() {
    auto x{1};
    return 0;
}
