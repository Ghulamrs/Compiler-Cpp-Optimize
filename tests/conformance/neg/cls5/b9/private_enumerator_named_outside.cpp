// [class.access]/1: an enumerator of a private enumeration is a private member, named here from main.
class S {
    enum Mode { off, on };
public:
    static int count() { return 2; }
};
int main() {
    return S::on + S::count();
}
