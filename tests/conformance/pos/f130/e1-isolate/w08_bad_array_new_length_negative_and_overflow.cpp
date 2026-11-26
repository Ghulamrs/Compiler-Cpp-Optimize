#include <cstdio>
#include <new>
struct Big { char c[1024]; };
int main() { int n = -1; try { new int[n]; std::printf("no1 "); } catch (std::bad_array_new_length &) { std::printf("banl1 "); } catch (std::bad_alloc &) { std::printf("ba1 "); } long long m = 1LL << 61; try { new Big[m]; std::printf("no2 "); } catch (std::bad_array_new_length &) { std::printf("banl2 "); } catch (std::bad_alloc &) { std::printf("ba2 "); } std::printf("\n"); return 0; }
