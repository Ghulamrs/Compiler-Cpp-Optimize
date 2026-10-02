// Assignment is the second door, `c = 2;`, and a literal is no exception.
enum Colour { Red, Green, Blue };
int main(void) {
    Colour c = Red;
    c = 2;
    return c;
}
