// [meta.unary.prop]/3: is_pod's operand must be complete; this one is only declared.
struct Later;
int main() { return __is_pod(Later); }
