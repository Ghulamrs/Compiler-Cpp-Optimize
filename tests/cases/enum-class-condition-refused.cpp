// [dcl.enum]/10: no contextual conversion to bool either - `if (c)` is ill-formed for a scoped
// enumeration. clang: "value of type 'Colour' is not contextually convertible to 'bool'" (5:9).
enum class Colour { Red, Green };
int main() {
    Colour c = Colour::Green;
    if (c) return 1;
    return 0;
}
