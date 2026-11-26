// [class.access]/1: a private typedef named qualified from a function that is neither a member nor a friend.
class S {
    typedef int T;
public:
    static int make() { return 3; }
};
int main() {
    S::T x = S::make();
    return x;
}
