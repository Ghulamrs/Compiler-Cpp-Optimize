#include <array>
#include <cstdio>
#include <memory>
#include <string>
#include <utility>
struct Base { int b; Base(int b) : b(b) {} virtual ~Base() {} };
struct Derived : Base { int d; Derived(int b, int d) : Base(b), d(d) {} };
struct Other : Base { Other() : Base(9) {} };
int main() {
    std::pair<int, std::string> p(4, "four");
    std::get<0>(p) += 1;
    std::get<1>(p) += "!";
    const std::pair<int, std::string> &cp = p;
    std::printf("%d %s %d\n", std::get<0>(cp), std::get<1>(cp).c_str(), (int)std::get<1>(cp).size());
    std::string moved = std::get<1>(std::move(p));
    std::printf("%s [%s]\n", moved.c_str(), p.second.c_str());
    std::array<int, 3> a = {{7, 8, 9}};
    std::get<2>(a) = 10;
    const std::array<int, 3> &ca = a;
    std::printf("%d %d %d\n", std::get<0>(ca), std::get<1>(a), std::get<2>(ca));
    std::array<std::string, 2> s = {{"x", "y"}};
    std::string taken = std::get<1>(std::move(s));
    std::printf("%s [%s]\n", taken.c_str(), s[1].c_str());
    std::shared_ptr<Base> b(new Derived(1, 2));
    std::shared_ptr<Derived> d = std::static_pointer_cast<Derived>(b);
    std::shared_ptr<Derived> d2 = std::dynamic_pointer_cast<Derived>(b);
    std::shared_ptr<Base> o(new Other());
    std::shared_ptr<Derived> none = std::dynamic_pointer_cast<Derived>(o);
    std::printf("%d %d %ld %d\n", d->d, d2->b, b.use_count(), (int)(none == nullptr));
    std::shared_ptr<const int> c(new int(3));
    std::shared_ptr<int> m = std::const_pointer_cast<int>(c);
    *m = 4;
    std::printf("%d %ld\n", *c, c.use_count());
    std::printf("%d %d\n", (int)(std::get_deleter<std::default_delete<int> >(m) != 0), (int)(std::get_deleter<std::default_delete<char> >(m) != 0));
    std::shared_ptr<int> plain(new int(1));
    std::shared_ptr<int> custom(new int(2), std::default_delete<int>());
    std::printf("%d %d\n", (int)(std::get_deleter<std::default_delete<int> >(plain) != 0),
        (int)(std::get_deleter<std::default_delete<int> >(custom) != 0));
    return 0;
}
