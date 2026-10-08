// [dcl.enum]/2: a scoped enumeration's enumerators are in its own scope, so `Green` alone names
// nothing here. clang: "use of undeclared identifier 'Green'" (5:12).
enum class Colour { Red, Green };
int main() {
    return Green == Colour::Green ? 0 : 1;
}
