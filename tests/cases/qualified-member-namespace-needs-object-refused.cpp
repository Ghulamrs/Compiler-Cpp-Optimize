// A non-static data member named through a namespace and two classes, with no object.
namespace n { namespace m { struct S { struct In { int y; }; }; } }
int main() { return n::m::S::In::y; }
