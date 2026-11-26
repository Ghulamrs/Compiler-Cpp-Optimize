// [dcl.fct.def.delete]/2: the primary is deleted, and int has no specialization of its own.
template <class T> void gen(T) = delete;
template <> void gen(char) {}
int main() { gen(1); return 0; }