#include <initializer_list>
int main() { auto l = {1, 2, 3}; const auto &r = {5, 6}; return (int)l.size() + (int)r.size() == 5 ? 0 : 1; }
