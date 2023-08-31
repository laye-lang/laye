#include "./test/block.h"
#include "./test/var.h"
#define NICE 69
#define STR_(Y) # Y
#define STR(X) STR_(X)
const char* number = STR(NICE);
const char* word = STR_(NICE);
int main() BLOCK
int foo1() { VAR(bar); }
int foo2() { VAR2(bar, STR(baz)); }
