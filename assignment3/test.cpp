#include <stdio.h>
int main() {
    int a = 0, b = 1;
    a = b;

    int c = 0;
    int* d = &b;
    c = *d;

    int* e = &b;
    int f = 0;
    *e = f;

    printf("%d, %d, %d, %d, %d, %d\n", a, b, c, *d, *e, f);
    return 0;    
}
