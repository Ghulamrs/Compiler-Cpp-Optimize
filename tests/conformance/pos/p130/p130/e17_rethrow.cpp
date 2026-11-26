int main() { try { try { throw 1; } catch (int) { throw; } } catch (int e) { return e == 1 ? 0 : 1; } return 1; }
