// [basic.lookup.qual]/4: x is N's, and the global namespace has none.
namespace N { int x = 1; }
int main() { return ::x; }