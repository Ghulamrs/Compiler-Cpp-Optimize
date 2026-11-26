#include <chrono>
int main() { std::chrono::duration<int, std::ratio<-1> > d; return d.count(); }
