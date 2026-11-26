#include <iostream>
#include <sstream>
#include <climits>
template <class T> void show(const char *what, const char *text) {
    std::istringstream in(text);
    T v = 0;
    in >> v;
    std::cout << what << ' ' << v << ' ' << in.fail() << '\n';
}
int main() {
    show<int>("int", "999999999999 ");
    show<int>("int-", "-999999999999 ");
    show<int>("int max", "2147483647 ");
    show<int>("int min", "-2147483648 ");
    show<int>("int max+1", "2147483648 ");
    show<int>("int min-1", "-2147483649 ");
    show<long>("long", "99999999999999999999 ");
    show<long long>("long long", "9223372036854775808 ");
    show<long long>("long long-", "-9223372036854775809 ");
    show<long long>("long long min", "-9223372036854775808 ");
    show<long long>("long long huge", "123456789012345678901234567890 ");
    show<unsigned>("unsigned", "4294967296 ");
    show<unsigned>("unsigned max", "4294967295 ");
    show<unsigned long long>("ull", "18446744073709551616 ");
    show<unsigned long long>("ull max", "18446744073709551615 ");
    show<int>("plain", "-17 ");
    show<unsigned>("unsigned neg", "-1 ");
    show<unsigned long long>("ull neg", "-5 ");
    show<unsigned>("unsigned neg big", "-4294967296 ");
    show<long>("long neg", "-2147483649 ");
    show<int>("sign only", "- ");
    show<int>("plus", "+12 ");
    std::istringstream h("7fffffff 80000000 ");
    int a = 0, b = 0;
    h >> std::hex >> a;
    std::cout << "hex " << a << ' ' << h.fail();
    h >> b;
    std::cout << ' ' << b << ' ' << h.fail() << '\n';
    std::istringstream after("99999999999 5");
    int x = 1, y = 1;
    after >> x >> y;
    std::cout << "after " << x << ' ' << y << ' ' << after.fail() << '\n';
    return 0;
}
