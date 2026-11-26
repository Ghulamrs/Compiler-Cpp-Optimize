#include <utility>
struct M { M() {} M(M &&) {} M(const M &) = delete; };
struct H { const M m; H() {} };
int main() {
    H a;
    H b(std::move(a));
    return 0;
}
