// [class.friend]/3: `friend T;` befriends the class T names, and does nothing where it names none or a
// fundamental type - the same template works for both.
#include <cstdio>
class Key;
template <class T> class Door {
    friend T;
    friend int;
    int code;
public:
    Door() : code(41) {}
};
typedef Key KeyAlias;
class Lock {
    friend KeyAlias;
    int pin;
public:
    Lock() : pin(9) {}
};
class Key {
public:
    int open(const Door<Key> &d) const { return d.code + 1; }
    int read(const Lock &l) const { return l.pin; }
};
int main() {
    Door<Key> d;
    Door<int> plain;
    Door<double> other;
    (void)plain;
    (void)other;
    Key k;
    Lock l;
    std::printf("%d %d\n", k.open(d), k.read(l));
    return 0;
}
