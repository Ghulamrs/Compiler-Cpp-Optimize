// The C6000 scheduler at -O2: a double result lands low word first, a cycle before its high word. 1.63 and 1.64 issued
// `LDBU *m, A4` and, one cycle on, `INTDP A16, A5:A4` - the load and the conversion's low word reached A4 in the same
// cycle (SPRUFE8 3.8.8), and sim6747 stopped there: the loop below, whose `t.add(name, i)` makes a double of an int
// after the flag of the temporary string before it is read. The examples' cpp-table, in one file; on x86-64 it is a
// plain program. Run on the C6747 by RTS6x's tests\c6747.ps1 too.
#include <cstdio>
#include <string>

class Table {
public:
    enum { kMost = 12 };
    Table() : held_(0) {}
    bool add(const std::string &name, double value);
    int held() const { return held_; }
    bool find(const std::string &name, double &into) const;

private:
    std::string names_[kMost];
    double values_[kMost];
    int held_;
};

bool Table::add(const std::string &name, double value) {
    if (held_ == kMost) return false;
    for (int i = 0; i < held_; ++i)
        if (names_[i] == name) return false;
    names_[held_] = name;
    values_[held_] = value;
    ++held_;
    return true;
}

bool Table::find(const std::string &name, double &into) const {
    for (int i = 0; i < held_; ++i)
        if (names_[i] == name) { into = values_[i]; return true; }
    return false;
}

int main() {
    Table t;
    t.add("pi", 3.14159);
    t.add("e", 2.71828);
    t.add("root2", 1.41421);

    double v = 0;
    if (t.find("e", v)) std::printf("e      %.5f\n", v);
    if (!t.find("tau", v)) std::printf("tau    not in the table\n");
    std::printf("pi again: %s\n", t.add("pi", 3.0) ? "added" : "refused, one entry per name");

    char name[8];
    int refused = 0;
    for (int i = 0; i < 12; ++i) {
        std::snprintf(name, sizeof name, "n%d", i);
        if (!t.add(name, i)) ++refused;
    }
    std::printf("held %d of %d, %d refused\n", t.held(), static_cast<int>(Table::kMost), refused);

    double sum = 0;
    for (int i = 0; i < 12; ++i) {
        std::snprintf(name, sizeof name, "n%d", i);
        if (t.find(name, v)) sum += v;
    }
    std::printf("sum of those held %.1f\n", sum);
    return 0;
}
