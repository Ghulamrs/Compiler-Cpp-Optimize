// A small text adventure driven by a state machine: switch with fall-through,
// const variables, #define constants and text, overloaded free functions,
// unsigned and short and char arithmetic, an inventory class, and a scripted
// walk through the rooms.
// Output through Compiler++'s natives rather than its <iostream>, which is compiled into
// every program that includes it and was 40% of this file's compile time.
void print_int(int n); void print_double(double d); void print_string(char *s);
void print_char(char c); void print_line();
void show(int n) { print_int(n); }
void show(double d) { print_double(d); }
void show(char *s) { print_string(s); }
void show(char c) { print_char(c); }
void show(bool b) { if (b) { print_int(1); } else { print_int(0); } }
void show(long n) { print_int((int)n); }
void show(unsigned int n) { print_int((int)n); }
void show(unsigned short n) { print_int((int)n); }
void show(float f) { print_double(f); }
#define ROOMS 6
#define START 0
#define TITLE "The Cellar"

const int NORTH = 0;
const int EAST = 1;
const int SOUTH = 2;
const int WEST = 3;
const int NONE = -1;

int exits[ROOMS][4];
char roomName[ROOMS][12];
unsigned short visits[ROOMS];

void setName(int r, char *s) { int i = 0; while (s[i] != 0) { roomName[r][i] = s[i]; i++; } roomName[r][i] = 0; }

void link(int a, int dir, int b) {
  exits[a][dir] = b;
  int back = (dir + 2) % 4;
  exits[b][back] = a;
}

void build() {
  for (int r = 0; r < ROOMS; r++) { for (int d = 0; d < 4; d++) { exits[r][d] = NONE; } visits[r] = 0; }
  setName(0, "cellar");
  setName(1, "hall");
  setName(2, "kitchen");
  setName(3, "library");
  setName(4, "garden");
  setName(5, "tower");
  link(0, NORTH, 1);
  link(1, EAST, 2);
  link(1, WEST, 3);
  link(2, NORTH, 4);
  link(3, NORTH, 5);
  link(4, WEST, 5);
}

class Inventory {
  int items[8];
  int n;
public:
  Inventory() { n = 0; }
  bool add(int item) { if (n == 8) { return false; } items[n] = item; n++; return true; }
  bool has(int item) const { for (int i = 0; i < n; i++) { if (items[i] == item) { return true; } } return false; }
  int count() const { return n; }
  int weight() const { int w = 0; for (int i = 0; i < n; i++) { w = w + items[i] * 3 + 1; } return w; }
};

int copyScript(char *to, char *from) { int n = 0; while (from[n] != 0) { to[n] = from[n]; n++; } to[n] = 0; return n; }

int score(int room) { return room * 10 + visits[room]; }
int score(int room, int bonus) { return score(room) + bonus; }
double score(double base, int room) { return base * 1.5 + room; }

int direction(char c) {
  switch (c) {
    case 'n': case 'N': return NORTH;
    case 'e': case 'E': return EAST;
    case 's': case 'S': return SOUTH;
    case 'w': case 'W': return WEST;
    default: return NONE;
  }
}

int main() {
  build();
  Inventory bag;
  int room = START;
  int points = 0;
  unsigned int steps = 0;
  char script[40];
  int len = copyScript(script, "neswnnwseeswnnwssenwnn");
  for (int k = 0; k < len; k++) {
    int d = direction(script[k]);
    if (d == NONE) { continue; }
    int to = exits[room][d];
    if (to == NONE) { points = points - 1; continue; }
    room = to;
    visits[room]++;
    steps++;
    switch (room) {
      case 2: if (!bag.has(1)) { bag.add(1); points = points + 5; }
      case 4: points = points + 1; break;
      case 5:
        if (bag.has(1)) { points = score(room, 20) + points; bag.add(2); }
        break;
      default: points = points + score(room) % 3;
    }
  }
  show(TITLE); show(": "); show(roomName[room]); show(" steps "); show(steps); show(" points "); show(points); print_line();
  show("bag "); show(bag.count()); show(" weight "); show(bag.weight()); show(" bonus "); show(score(2.5, room)); print_line();
  for (int r = 0; r < ROOMS; r++) { show(roomName[r]); show("="); show(visits[r]); show(" "); }
  print_line();
  return 0;
}
