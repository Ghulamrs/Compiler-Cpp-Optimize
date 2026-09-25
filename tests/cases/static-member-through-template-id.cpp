// A static data member named through the template's argument list is the
// refusal EXCLUSIONS names, and the message says so (the cl review's A23).
template <class T> struct Box { static int count; };
template <class T> int Box<T>::count = 0;
int main() { Box<int>::count = 3; return Box<int>::count; }
