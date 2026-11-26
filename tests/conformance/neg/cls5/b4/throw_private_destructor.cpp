class E { ~E() {} public: E() {} E(const E &) {} };
int main() { throw E(); }
