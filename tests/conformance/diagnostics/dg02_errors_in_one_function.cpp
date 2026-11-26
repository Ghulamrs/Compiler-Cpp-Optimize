// several in one function, and a declaration that fails with its name used after
int main() {
    int a = 1;
    a = nope1;
    int b = nope2;
    b = b + 1;
    a = nope3;
    if (a) { a = nope4; } else { a = nope5; }
    return a + b;
}
