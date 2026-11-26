// [meta.unary.prop]/3: every argument type of is_constructible must be complete.
struct Later;
struct S { S(const Later &); };
int main() { return __is_constructible(S, Later); }
