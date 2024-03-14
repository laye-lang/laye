#define CAT_(A, B) A##B
#define CAT(A, B) CAT_(A, B)
#define FOO foo
#define BAR bar
#include "block.h"
#include "var.h"
int main() BLOCK
void CAT_(FOO, BAR)() { VAR(baz); }
void CAT(FOO, bar)() { VAR2(baz, 69); }