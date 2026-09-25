// The refuse-by-name rule: a keyword this compiler recognises but has not
// implemented is refused where it stands, not twenty tokens later where the
// parse finally falls over.
//
// **This case names whichever keyword is still pending, and that changes as
// the ladder advances** - it said 'class' until rung 3 implemented it, and
// 'typeid' until 2026-09-14, which is how a green suite told the truth about
// the day before. Pick a keyword from a rung that is still ahead.
int main(void) { int x = thread_local; return x; }
