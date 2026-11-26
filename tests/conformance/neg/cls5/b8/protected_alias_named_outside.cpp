// [class.access]/1: a protected alias-declaration named from outside the class and its derived classes.
class B {
protected:
    using T = long;
};
int main() {
    B::T x = 0;
    return static_cast<int>(x);
}
