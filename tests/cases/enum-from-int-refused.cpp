// `Colour c = n;` for an int n, refused: [dcl.enum]/10 lets an enumeration
// become an integer and never the other way without a cast. C allows it.
enum Colour { Red, Green, Blue };
int main(void) {
    int n = 1;
    Colour c = n;
    return c;
}
