// [temp.res]/10: `missing` depends on nothing and is declared nowhere.
template <class T> int f(T) { return missing + 1; }
int main() { return 0; }
