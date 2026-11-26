#define F(a, ...) f(a, ## __VA_ARGS__)
int f(int, int);
int main() { return F(1, 2); }
