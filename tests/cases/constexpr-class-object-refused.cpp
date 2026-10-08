// A `constexpr` object of class type is legal C++11 and refused by name here: the constant
// evaluator folds integers and floating values and has no object to build. clang accepts it;
// `const P p(1, 2);` builds the same object at run time.
struct P { int x, y; constexpr P(int a, int b) : x(a), y(b) {} };
constexpr P origin(1, 2);
int main() { return origin.x - 1; }
