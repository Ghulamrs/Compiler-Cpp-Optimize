// [expr.const]/3: the address of an automatic object is no address constant.
int main() {
    int local = 1;
    constexpr int *p = &local;
    return *p;
}
