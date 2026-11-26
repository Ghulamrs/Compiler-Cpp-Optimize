#define S_(x) #x
#define S(x) S_(x)
int main() { const char *s = S(/* c */ z /* d */); return s[0] == 'z' && s[1] == 0 ? 0 : 1; }
