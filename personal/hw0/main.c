#include <stdio.h>
#include <sys/resource.h>

int main() {
    struct rlimit lim;
    int result;
    result = getrlimit(RLIMIT_STACK, &lim);

    if (result) {
      printf("Error getting RLIMIT_STACK");
      return 1;
    }

    printf("stack size: %ld\n", lim.rlim_cur);

    result = getrlimit(RLIMIT_NPROC, &lim);

    if (result) {
      printf("Error getting RLIMIT_NPROC");
      return 1;
    }

    printf("process limit: %ld\n", lim.rlim_cur);

    result = getrlimit(RLIMIT_NOFILE, &lim);

    if (result) {
      printf("Error getting RLIMIT_NOFILE");
      return 1;
    }

    printf("max file descriptors: %ld\n", lim.rlim_cur);
    return 0;
}
