// A missing semicolon is reported just after the statement that wants it, as clang reports it,
// not at the first token of the next line.
int main() {
    int x = 1
    return x;
}
