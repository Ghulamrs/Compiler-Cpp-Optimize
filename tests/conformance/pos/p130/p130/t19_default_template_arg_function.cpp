template <class T = int> T zero() { return T(); }
int main() { return zero() == 0 ? 0 : 1; }
