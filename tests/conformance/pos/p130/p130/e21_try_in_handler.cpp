int main() { try { throw 1; } catch (int) { try { throw 2; } catch (int e) { return e == 2 ? 0 : 1; } } return 1; }
