#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"


int main(void) {

  char ar[4096];
  for (int i = 0; i < 4096; i++) {
    ar[i] = i;
  }

  exit(ar[0]);
}
