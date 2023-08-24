#define BLOCK {}
#define VAR(A) int A = 10
#define VAR2(A, V) int A = (V)
int main() BLOCK
void foo() { VAR(bar); }
void foo() { VAR2(bar, 69); }
