// [dcl.enum]/5: an enumerator the enum-base cannot hold is ill-formed, not
// wrapped - it was 44 until 2026-09-14 (finding 4).
enum E : unsigned char { Y = 300 };
int main() { return Y; }
