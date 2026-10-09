// An alias declaration takes a type-id, which names nothing: `using X = int y;` declares a `y` of
// no standing. clang: "type-id cannot have a name" (3:17).
using X = int y;
int main() { X a = 0; return a; }
