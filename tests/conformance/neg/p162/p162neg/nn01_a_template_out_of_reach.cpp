// [basic.lookup.unqual]: `vector` is std's, and nothing brings it here.
#include <vector>
int main() { vector<int> v; return (int)v.size(); }
