// [class.copy]/7 and /11: a move written `= default` deletes the implicit copy, and a member
// whose copy is deleted deletes its class's. Both are declared deleted, so the use is refused
// where it stands. clang: "call to implicitly-deleted copy constructor of 'W'" (6:21).
struct U { U() {} U(const U &) = delete; };
struct W { U u; };
int main() { W a; W b = a; (void)b; return 0; }
