#include <strstream>

// [depr.strstreambuf]: strstreambuf has no accessible copy constructor.
int main() {
    std::strstreambuf a;
    std::strstreambuf b(a);
    return b.pcount();
}
