// [class.access]/1: an unnamed enumeration's enumerators take the access they were declared under.
class S {
    enum { size = 8 };
};
int main() {
    int a[S::size] = { 1 };
    return a[0];
}
