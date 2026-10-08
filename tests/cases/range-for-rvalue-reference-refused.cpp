// A loop variable written `T &&` over an array: each element is an lvalue, and an rvalue
// reference cannot bind to one - clang refuses it at the ':' (column 16), cpp11 at the name.
// `auto &&` is the spelling that works: it deduces `T &` (auto-forwarding-reference.cpp).
int main() {
    int arr[2] = { 1, 2 };
    for (int &&x : arr) x = 0;
    return arr[0];
}
