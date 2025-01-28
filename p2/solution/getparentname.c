#include "types.h"
#include "user.h"

int
main(void)
{
  char parentbuf[256];
  char childbuf[256];

  if (getparentname(parentbuf, childbuf, sizeof(parentbuf), sizeof(childbuf)) == 0) {
    printf(1, "XV6_TEST_OUTPUT Parent name: %s Child name: %s", parentbuf, childbuf);
  } else {
    printf(1, "Failed to get process names.\n");
  }

  exit();
}