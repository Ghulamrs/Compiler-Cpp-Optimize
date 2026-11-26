#include <sstream>

// [iostate.flags]/1: basic_ios's conversion to bool is explicit, so copy-initialisation cannot use it.
int main() {
    std::istringstream in("1");
    bool ok = in;
    return ok ? 0 : 1;
}
