// [temp.inst]/3: the candidate chosen is needed, and what its body refuses is refused.
struct W { template <class U> W(U) { static_assert(sizeof(U) == 0, "not for this"); } W(int) {} };
int main() { W a(1.5); (void)a; return 0; }
