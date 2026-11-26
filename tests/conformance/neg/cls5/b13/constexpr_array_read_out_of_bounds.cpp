// [expr.const]/2: an element past the end of a constexpr array is undefined to read, so no constant.
constexpr int table[3] = {1, 2, 3};
constexpr int past = table[3];
int main() {
    return past;
}
