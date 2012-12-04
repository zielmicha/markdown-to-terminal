#include <term.h>
#include <stdio.h>
#include <string.h>

int main() {
  setupterm(NULL, 1, NULL);
  const char* setf = tigetstr("setaf");
  //  putp(setf);
  const char* red = tparm(setf, 1);
  putp(red);
  printf("whooo!\n");
  return 1;
}
