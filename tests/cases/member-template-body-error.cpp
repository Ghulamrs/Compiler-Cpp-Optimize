// An error inside an instantiated member template's body is reported, naming the place that
// asked for the instantiation - it used to be swallowed by the trial around the candidate, which
// dropped the function and left the call for the linker to notice.
template <int N> struct V {
    int a[N];
    template <int K> int operator*(const V<K> &rhs) const {
        return a[0] * rhs.a[0] + undeclared_name;
    }
};
int main() {
    V<3> x, y;
    return x * y;
}
