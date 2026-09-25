// **What compiling the Shalimar runtime for the C6000 asked of this compiler**
// (2026-09-14): a static const member as an array bound in its own class,
// which reaches the class's private names; `= {0}` and `= {}` zeroing a
// member; `[[noreturn]]` read; `N::x` reaching N's unnamed namespace, for a
// function, a class and a type name; a reference loop variable in a
// range-based for; an enumerator in a member initialiser; <cstdint>,
// std::isnan, std::trunc and string::append(n, c).
#include <cstdint>
#include <cmath>
#include <string>
#include <vector>
extern "C" int printf(const char *, ...);

namespace shm {
namespace {
int narrow(long long wide) { return static_cast<int>(wide); }
class Depth { public: int total() const { return 7; } };
}
int use() { return narrow(3); }
}

class Position {
public:
    int count() const { return capacity; }
    int32_t first() const { return units_[0]; }
    int mode() const { return mode_; }
private:
    static const int capacity = 5;
    enum Mode { Running, Stepping };
    int32_t units_[capacity] = {0};
    const char *names_[capacity] = {};
    Mode mode_ = Stepping;
};

[[noreturn]] void stop(int code) { printf("stop %d\n", code); for (;;) { } }

int main() {
    Position p;
    shm::Depth d;
    printf("%d %d %d %d %d\n", p.count(), p.first(), p.mode(), shm::use() + shm::narrow(4), d.total());
    std::vector<std::string> rows;
    rows.push_back("ab"); rows.push_back("cde");
    int total = 0;
    for (const std::string &row : rows) total += (int)row.size();
    for (std::string &row : rows) row.append(2, '.');
    printf("%d %s %s\n", total, rows[0].c_str(), rows[1].c_str());
    double nan = 0.0 / 0.0;
    printf("%d %d %d %d\n", std::isnan(nan), std::isinf(1.0 / 0.0), (int)std::trunc(-2.7), (int)std::round(2.5));
    printf("%d %d\n", (int)sizeof(int64_t), (int)(INT32_MAX == 2147483647));
    if (total < 0) stop(1);
    return 0;
}
