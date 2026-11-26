// [class.dtor]/5: a class whose base's destructor is private has its implicit destructor deleted; destroying one
// is ill-formed.
class Base {
    ~Base() {}
};
class Derived : public Base {};

int main() {
    Derived d;
    return 0;
}
