// [dcl.init.list]/7: the inner list narrows as any other would.
#include <initializer_list>
int main() {
    std::initializer_list<std::initializer_list<int> > x = {{1, 2.5}};
    return (int)x.size();
}
