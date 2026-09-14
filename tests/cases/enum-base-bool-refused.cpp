// [dcl.enum]/5: the underlying type is an integral type other than bool.
enum Flag : bool { Off, On };
int main() { return Off; }
