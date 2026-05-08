#include <stdio.h>

int add(int a, int b) {
    return a + b;
}

int multiply(int a, int b) {
    return a * b;
}

int main() {
    int x = 5, y = 3;
    printf("add(%d,%d) = %d\n", x, y, add(x, y));
    printf("multiply(%d,%d) = %d\n", x, y, multiply(x, y));
    return 0;
}
