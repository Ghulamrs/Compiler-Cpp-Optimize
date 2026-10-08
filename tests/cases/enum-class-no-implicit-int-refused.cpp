// [dcl.enum]/10: a scoped enumeration does not convert to an integer implicitly - `int i = c;`
// needs a cast. clang: "cannot initialize a variable of type 'int' with an lvalue of type
// 'Colour'" (6:9).
enum class Colour { Red, Green };
int main() {
    Colour c = Colour::Green;
    int i = c;
    return i;
}
