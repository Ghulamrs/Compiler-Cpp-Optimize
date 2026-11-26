template <class... Ts> int sum(Ts... ts) { return (ts + ...); } int main() { return sum(1, 2); }
