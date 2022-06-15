#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int fib(int n) {
  if (n == 0) {
    return 0;
  }
  if (n == 1) {
    return 1;
  }

  return fib(n-1) + fib(n-2);
}

int main(void) {

  int f = fib(40);
  printf("%d \n", f);
  exit(0);
}
