constexpr int tbl[3] = {1, 2, 3};
static_assert(tbl[1] == 2, "tbl");
int g;
constexpr int *pg = &g;
int main() { return pg == &g ? 0 : 1; }
