//#define CAT_(A, B) A##B
//#define CAT(A, B) CAT_(A, B)
#define FOO foo
#define BAR bar
#include "block.h"
#include "var.h"
int main() BLOCK
void FOO() { VAR(baz); }
void BAR() { VAR2(baz, 69); }