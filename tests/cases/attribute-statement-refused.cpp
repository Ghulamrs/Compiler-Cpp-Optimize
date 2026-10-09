// [dcl.attr.grammar] lets an attribute stand before any statement; this compiler reads one before
// a declaration only, and says so. clang accepts the program (an unknown attribute is a warning).
int main() {
    int n = 1;
    [[tag]] n = 2;
    return n - 2;
}
