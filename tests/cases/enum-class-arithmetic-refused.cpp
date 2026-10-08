// [expr]/10's usual arithmetic conversions start from an unscoped enumeration: two scoped
// enumerators compare and do not add. clang: "invalid operands to binary expression ('Flag'
// and 'Flag')" (5:32).
enum class Flag { A = 1, B = 2 };
int main() { Flag f = Flag::A | Flag::B; return f == Flag::A; }
