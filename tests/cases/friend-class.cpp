// `friend class X;` - [class.friend]/2: every member of X, a nested class of X included, reaches
// what the class keeps private - its data, its member functions, its constructor. X may be
// declared by the friend declaration itself, and `friend X;` names one already declared.
// Friendship is not inherited and not transitive (friend-class-not-friend-refused). clang
// agrees on every line.
extern "C" int printf(const char *, ...);

class Box {
    friend class Peeker;
    friend struct Opener;
    int secret;
    int twice() const { return secret * 2; }
    Box(int s) : secret(s) {}
public:
    Box() : secret(1) {}
};

class Peeker {
public:
    int look(const Box &b) const { return b.secret + b.twice(); }
    static Box make(int s) { return Box(s); }
    struct Inner { int see(const Box &b) const { return b.secret; } };
};

struct Opener { int open(Box &b) { b.secret = 40; return b.secret; } };

class Vault;
class Keeper { public: int peek(const Vault &v) const; };
class Vault { friend Keeper; int gold = 9; };
int Keeper::peek(const Vault &v) const { return v.gold; }

int main() {
    Box b;
    Peeker p;
    int a = p.look(b);
    Box made = Peeker::make(5);
    int c = p.look(made);
    printf("%d %d\n", a, c);
    Opener o;
    int d = o.open(b);
    Peeker::Inner in;
    int e = in.see(b);
    printf("%d %d\n", d, e);
    Vault v;
    Keeper k;
    int g = k.peek(v);
    printf("%d\n", g);
    return 0;
}
