#include "test/block.h"
#include "test/var.h"
int main() BLOCK
void foo() { VAR(bar); }
void foo() { VAR2(bar, 69); }
