// [namespace.std]: a program's templates of the library's names, at global scope, change nothing the library does with its own.
#include <algorithm>
#include <cstdio>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>
template <bool B, class T = void> struct enable_if { int mine; };
template <class A, class B> struct is_same { static const int value = 77; };
template <class T> struct allocator { int mine; };
template <class T> struct less { int mine; };
template <class T> struct char_traits { int mine; };
template <class T, T v> struct integral_constant { static const int value = 88; };
template <class T> struct remove_reference { typedef int *type; };
template <class T> struct decay { typedef int *type; };
template <class A, class B> struct pair { A a; B b; };
template <class T> struct vector { T one; };
template <class T> struct function { int mine; };
template <class... T> struct tuple { int mine; };
template <class T> struct shared_ptr { int mine; };
template <class T> struct basic_string { int mine; };
template <class T> struct iterator_traits { int mine; };
template <class T> struct move_iterator { int mine; };
template <class T> struct hash { int mine; };
int main() {
    std::vector<int> v(3, 1);
    std::sort(v.begin(), v.end());
    std::map<std::string, std::vector<int> > m;
    m["a"].push_back(2);
    std::set<int> s(v.begin(), v.end());
    std::unordered_map<std::string, int> u;
    u["k"] = 4;
    std::function<int(int)> f = [](int x) { return x + 1; };
    std::tuple<int, std::string> t(1, "x");
    std::shared_ptr<int> p = std::make_shared<int>(5);
    std::pair<int, char> q = std::make_pair(1, 'c');
    std::ostringstream o;
    o << v.size() << ' ' << *p;
    std::regex r("a+");
    std::thread th([] {});
    th.join();
    vector<int> mine = { 6 };
    std::cout << o.str() << ' ' << m.size() << ' ' << s.size() << ' ' << u["k"] << ' ' << f(1) << ' ' << std::get<0>(t) << ' '
              << q.first << ' ' << std::regex_match("aa", r) << ' ' << mine.one << ' ' << is_same<int, int>::value << ' '
              << std::is_same<int, int>::value << ' ' << integral_constant<int, 1>::value << '\n';
    return 0;
}
