class E { E(const E &); public: E() {} }; int main() { throw E(); }
