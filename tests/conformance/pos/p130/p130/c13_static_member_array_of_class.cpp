struct T { int v; T() : v(1) {} };
struct S { static T arr[2]; };
T S::arr[2];
int main() { return S::arr[1].v == 1 ? 0 : 1; }
