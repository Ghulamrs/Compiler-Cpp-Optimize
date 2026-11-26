// [namespace.qual]/2: `user::f` is looked up in user alone, which declares no f - other's template is not reached.
namespace other { template <class T> int f(T) { return 1; } }
namespace user { struct U {}; }
int main() { user::U u; return user::f(u); }
