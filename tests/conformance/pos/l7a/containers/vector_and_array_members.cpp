#include <array>
#include <cstdio>
#include <initializer_list>
#include <memory>
#include <string>
#include <utility>
#include <vector>
void show(const char *what, const std::vector<int> &v) {
    std::printf("%s:", what);
    for (std::vector<int>::const_iterator i = v.cbegin(); i != v.cend(); ++i) std::printf(" %d", *i);
    std::printf("\n");
}
int main() {
    std::vector<int, std::allocator<int> > v{1, 2, 3};
    std::vector<int>::value_type first = v.front();
    std::vector<int>::difference_type span = v.end() - v.begin();
    std::vector<int>::reference r = v[1];
    std::vector<int>::pointer p = v.data();
    std::printf("%d %d %d %d\n", first, (int)span, r, p[2]);
    v.insert(v.cbegin() + 1, 2, 9);
    show("insert(n)", v);
    std::initializer_list<int> extra = {7, 8};
    v.insert(v.cend(), extra);
    show("insert(il)", v);
    v.insert(v.cbegin(), {5, 6});
    show("insert({})", v);
    v.insert(v.begin() + 2, 4);
    v.erase(v.cbegin() + 3, v.cbegin() + 5);
    v.erase(v.cbegin());
    show("erase", v);
    std::printf("rbegin:");
    for (std::vector<int>::reverse_iterator i = v.rbegin(); i != v.rend(); ++i) std::printf(" %d", *i);
    std::printf(" |");
    for (std::vector<int>::const_reverse_iterator i = v.crbegin(); i != v.crend(); ++i) std::printf(" %d", *i);
    std::printf("\n");
    v.assign({4, 3});
    show("assign(il)", v);
    std::initializer_list<int> again = {1, 1, 2};
    v = again;
    show("operator=(il)", v);
    v.reserve(40);
    std::printf("%d", (int)(v.capacity() >= 40));
    v.shrink_to_fit();
    std::printf(" %d %d\n", (int)(v.capacity() == v.size()), (int)(v.max_size() >= v.size()));
    std::vector<int> w(std::allocator<int>{});
    std::allocator<int> al = w.get_allocator();
    int *one = al.allocate(1);
    al.deallocate(one, 1);
    std::printf("%d\n", (int)w.empty());
    std::vector<int> a1{1, 2, 3}, a2{1, 2, 4};
    std::printf("%d %d %d %d\n", (int)(a1 < a2), (int)(a1 > a2), (int)(a1 <= a1), (int)(a2 >= a1));
    swap(a1, a2);
    std::printf("%d\n", a1[2]);
    std::vector<std::string> s{"x", "y"};
    s.insert(s.cbegin() + 1, std::string("mid"));
    std::printf("%s %s %s\n", s[0].c_str(), s[1].c_str(), s[2].c_str());
    std::array<int, 4> arr = {{3, 1, 4, 1}};
    std::printf("array:");
    for (std::array<int, 4>::reverse_iterator i = arr.rbegin(); i != arr.rend(); ++i) std::printf(" %d", *i);
    std::printf(" | %d %d\n", *arr.crbegin(), (int)std::tuple_size<std::array<int, 4> >::value);
    std::tuple_element<2, std::array<int, 4> >::type t = arr[2];
    std::array<int, 4> arr2 = {{3, 1, 5, 0}};
    std::printf("%d %d %d %d %d\n", t, (int)(arr < arr2), (int)(arr2 > arr), (int)(arr <= arr), (int)(arr >= arr2));
    swap(arr, arr2);
    std::printf("%d\n", arr[2]);
    return 0;
}
