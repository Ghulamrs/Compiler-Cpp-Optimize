// Only a lambda that captures nothing has the conversion to a function pointer.
int main() {
    int k = 3;
    int (*f)(int) = [k](int x) { return x + k; };
    return f(1);
}
