#include <cstdio>
#include <type_traits>
struct Data { int m; void f(); };
struct Functor {
    double operator()(int) const { return 1.5; }
    char operator()(const char *) const { return 'c'; }
};
int twice(int x) { return 2 * x; }
long &pick(long &x) { return x; }
typedef int (*IntFn)(int);
#define P(...) std::printf("%d", (int)(__VA_ARGS__))
int main() {
    P(std::is_function<void(int)>::value); P(std::is_function<int(double, ...)>::value);
    P(std::is_function<int (*)(int)>::value); P(std::is_function<int (&)(int)>::value);
    P(std::is_function<int>::value); P(std::is_function<Functor>::value);
    std::printf("\n");
    P(std::is_object<void()>::value); P(std::is_compound<void()>::value); P(std::is_scalar<void()>::value);
    P(std::is_member_function_pointer<void (Data::*)()>::value); P(std::is_member_object_pointer<int Data::*>::value);
    P(std::is_member_pointer<int Data::*>::value); P(std::is_member_pointer<void (Data::*)()>::value);
    std::printf("\n");
    P(std::is_same<std::decay<int(int)>::type, int (*)(int)>::value);
    P(std::is_same<std::decay<int (&)(int)>::type, int (*)(int)>::value);
    P(std::is_same<std::add_pointer<void(int)>::type, void (*)(int)>::value);
    P(std::is_same<std::add_lvalue_reference<void(int)>::type, void (&)(int)>::value);
    std::printf("\n");
    P(std::is_same<std::result_of<IntFn(int)>::type, int>::value);
    P(std::is_same<std::result_of<Functor(int)>::type, double>::value);
    P(std::is_same<std::result_of<const Functor(const char *)>::type, char>::value);
    P(std::is_same<std::result_of<decltype(&twice)(short)>::type, int>::value);
    std::printf("\n");
    std::result_of<Functor(int)>::type r = Functor()(3);
    std::printf("%g %d\n", r, twice(4));
    return 0;
}
