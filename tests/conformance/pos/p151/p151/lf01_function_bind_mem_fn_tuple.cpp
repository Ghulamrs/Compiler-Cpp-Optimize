// [func.wrap], [func.bind], [func.memfn], [refwrap], [tuple], [unord.hash]: function, bind with placeholders and
// nested binds, mem_fn, ref, tuple construction, get, tie, ignore, tuple_cat, comparisons, the C++98 adaptors.
#include <cstdio>
#include <functional>
#include <tuple>
#include <string>
#include <utility>
struct S {
    int v;
    int get() const { return v; }
    int add(int a) { return v + a; }
};
struct Twice { int operator()(int x) const { return 2 * x; } };
int square(int x) { return x * x; }
int sub(int a, int b) { return a - b; }
void bump(int &x) { x += 10; }
int main() {
    std::function<int(int)> f = square;
    std::function<int(int)> g = Twice();
    std::function<int(int)> h = [](int x) { return x + 100; };
    std::function<int(int)> e;
    std::printf("%d %d %d %d %d\n", f(5), g(5), h(5), (int)static_cast<bool>(e), (int)(e == nullptr));
    try { e(1); } catch (const std::bad_function_call &x) { std::printf("caught %s\n", x.what()); }
    std::function<int(const S &)> mg = &S::get;
    std::function<int(S &, int)> ma = &S::add;
    std::function<int(S &)> mv = &S::v;
    S s = {7};
    std::printf("%d %d %d %d\n", mg(s), ma(s, 3), mv(s), (int)(f.target<int (*)(int)>() != nullptr));
    std::function<int(int)> c = f;
    c = g;
    std::swap(c, f);
    std::printf("%d %d\n", c(3), f(3));
    using namespace std::placeholders;
    std::function<int(int)> b1 = std::bind(sub, _1, 4);
    auto b2 = std::bind(sub, _2, _1);
    auto b3 = std::bind(sub, std::bind(square, _1), 1);
    auto b4 = std::bind(&S::add, _1, 5);
    std::printf("%d %d %d %d %d\n", b1(10), b2(1, 9), b3(4), b4(s), (int)std::bind<long>(square, 6)());
    int k = 1;
    std::bind(bump, std::ref(k))();
    auto m = std::mem_fn(&S::get);
    auto mp = std::mem_fn(&S::v);
    std::printf("%d %d %d %d\n", k, m(s), mp(&s), std::ref(s).get().v);
    std::tuple<int, std::string, double> t(1, "two", 3.5);
    std::tuple<int, std::string, double> t2 = std::make_tuple(1, std::string("two"), 4.5);
    std::printf("%d %s %g %d %d %d\n", std::get<0>(t), std::get<1>(t).c_str(), std::get<2>(t),
                (int)std::tuple_size<decltype(t)>::value, (int)(t < t2), (int)(t == t2));
    int a = 0; std::string str; double d = 0;
    std::tie(a, str, d) = t2;
    std::tie(a, std::ignore, std::ignore) = std::make_tuple(9, 'x', 2.0);
    auto cat = std::tuple_cat(std::make_tuple(1, 'c'), std::make_tuple(2.5), std::make_tuple(std::string("z")));
    std::printf("%d %s %g %d %c %g %s\n", a, str.c_str(), d, std::get<0>(cat), std::get<1>(cat), std::get<2>(cat),
                std::get<3>(cat).c_str());
    std::tuple<int, int> p = std::make_pair(5, 6);
    std::tuple<int &, int &> rt(a, k);
    std::get<0>(rt) = 77;
    std::printf("%d %d %d\n", std::get<0>(p) + std::get<1>(p), a, (int)std::tuple_size<std::tuple<> >::value);
    std::printf("%d %d %d %d\n", (int)std::not1(std::logical_not<bool>())(true), (int)std::not2(std::less<int>())(1, 2),
                std::bind1st(std::minus<int>(), 10)(3), std::ptr_fun(square)(4));
    std::printf("%d %d %d\n", std::bit_and<int>()(6, 3), std::bit_or<int>()(6, 3), std::bit_xor<int>()(6, 3));
    std::printf("%d %d\n", (int)std::is_placeholder<decltype(_2)>::value, (int)std::is_bind_expression<decltype(b2)>::value);
    std::printf("%llu %llu %llu\n", (unsigned long long)std::hash<int>()(42), (unsigned long long)std::hash<double>()(1.5),
                (unsigned long long)(std::hash<long>()(0) == std::hash<long>()(0)));
    return 0;
}
