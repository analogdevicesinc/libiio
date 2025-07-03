#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "iio/iio.h"

int main() {

  char *expected_version = "1.0";
  char actual_version[10];

  sprintf(actual_version, "%u.%u", iio_context_get_version_major(NULL),
          iio_context_get_version_minor(NULL));

#ifdef TESTS_DEBUG
  printf("Expected version: %s\n", expected_version);
  printf("Actual version: %s\n", actual_version);
#endif

  assert(strcmp(expected_version, actual_version) == 0);

  return 0;
}