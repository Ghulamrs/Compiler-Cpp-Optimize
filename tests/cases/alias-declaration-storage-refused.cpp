// A storage class is not part of a type-id. clang: "type name does not allow storage class to be
// specified" (3:11).
using X = static int;
int main() { X a = 0; return a; }
