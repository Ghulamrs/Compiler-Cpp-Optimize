// [dcl.init.list]/7: a static initializer_list's elements are copy-list-initialised, so narrowing is refused.
#include <initializer_list>
static std::initializer_list<int> values = {1, 2.5, 3};
int main() {
    return static_cast<int>(values.size());
}
