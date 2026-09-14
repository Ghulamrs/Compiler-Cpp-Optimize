// `new (int)` - a parenthesised type-id where a placement is read. The two
// are the same tokens to the parser, and placement new is the form that is
// built (operator-new.cpp), so this one is refused with the spelling to use.
int main(void) {
    int *p = new (int);
    return p == 0;
}
