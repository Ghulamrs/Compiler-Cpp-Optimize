// [class.temporary]/5: a class temporary bound to a reference with static storage lives as long as the reference
// and is destroyed at exit, in reverse order of construction, beside the other static objects
// ([basic.start.term]/1); a reference to a base binds the base of the temporary's own class.
#include <cstdio>

struct Noisy {
    int id;
    explicit Noisy(int i) : id(i) { std::printf("+%d ", id); }
    virtual ~Noisy() { std::printf("-%d ", id); }
};
struct Derived : Noisy {
    explicit Derived(int i) : Noisy(i) {}
    ~Derived() { std::printf("~D%d ", id); }
};
struct Pair {
    int a, b;
};

static Pair makePair(int a, int b) {
    Pair p = { a, b };
    return p;
}

const Noisy &first = Noisy(1);
Noisy plain(2);
Noisy &&second = Noisy(3);
const Noisy &viaBase = Derived(4);
const Pair &fromCall = makePair(5, 6);

static const Noisy &local() {
    static const Noisy &kept = Noisy(7);
    return kept;
}

int main() {
    std::printf("| %d %d %d %d %d %d %d |", first.id, plain.id, second.id, viaBase.id, fromCall.a, fromCall.b,
                local().id);
    std::printf(" %d\n", local().id);
    return 0;
}
