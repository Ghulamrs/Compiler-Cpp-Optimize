// [dcl.attr.noreturn]/1: `noreturn` takes no argument clause. clang: "'noreturn' cannot have an
// argument list" (3:3).
[[noreturn(1)]] void die();
int main() { return 0; }
