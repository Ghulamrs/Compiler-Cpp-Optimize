// M10 W4: aggregates and C++ in cdb - arrays, structs, unions, enums, typedefs,
// a class with a base and member functions, a namespace, a reference, a template.
// Gate: `m10.py compare tests/m10/w4.cpp 37,47 --expr ...` equal to cl /Zi.
#include <stdio.h>

enum Colour { Red, Green = 5, Blue };
typedef unsigned int Count;

struct Point { int x; int y; };
union Bits { int i; float f; };

namespace geo {
struct Shape {
    int sides;
    int area() const { return sides * 10; }
};
}

class Square : public geo::Shape {
public:
    int edge;
    Square(int e) { sides = 4; edge = e; }
    int perimeter() const {
        int p = edge * sides;
        return p;
    }
};

template <typename T> struct Box { T value; T twice() const { return value + value; } };

int total(const Point &pt, int (&row)[3]) {
    int s = pt.x + pt.y;
    for (int i = 0; i < 3; i++) s += row[i];
    return s;
}

int main() {
    int row[3] = { 1, 2, 3 };
    Point pt = { 4, 5 };
    Bits bits;
    bits.i = 0x3f800000;
    Colour c = Green;
    Count n = 9;
    Square sq(6);
    Box<int> box = { 21 };
    char name[6] = "cdb";
    int t = total(pt, row) + sq.perimeter() + box.twice();
    printf("%d %d %d %u %s\n", t, sq.area(), (int)c, n, name);
    return t == 81 ? 0 : 1;
}
