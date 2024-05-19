#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fcntl.h"
#include "user/user.h"

void primes(int);

int main(int argc, char *argv[]) {
  int pid;
  int fds[2];
  int i;
  int wstat;

  if (pipe(fds) < 0) {
    fprintf(2, "pingpong: pipe failed\n");
    exit(1);
  }
  pid = fork();
  if (pid == 0) {
    // child
    close(fds[1]);
    primes(fds[0]);

    exit(0);
  } else if (pid > 0) {
    // parent
    close(fds[0]);

    for (i = 2; i <= 35; i++) {
      write(fds[1], &i, sizeof(int));
    }
    close(fds[1]);

    wait(&wstat);

    exit(0);
  } else {
    fprintf(2, "pingpong: fork failed\n");
    exit(1);
  }
}

void primes(int fd) {
  int pid;
  int fds[2];
  int first;
  int now;
  int wstat;

  read(fd, &first, sizeof(int));
  printf("prime %d\n", first);

  now = 0;
  while (read(fd, &now, sizeof(int)) != 0 && now % first == 0) {
    // if now is not a prime, set it to 0
    now = 0;
  }

  // exist next
  if (now != 0) {
    if (pipe(fds) < 0) {
      fprintf(2, "pingpong: pipe failed\n");
      exit(1);
    }

    pid = fork();
    if (pid == 0) {
      // child
      // child don't need fd
      close(fd);
      close(fds[1]);
      primes(fds[0]);

      exit(0);
    } else if (pid > 0) {
      // parent
      close(fds[0]);

      write(fds[1], &now, sizeof(int));
      while (read(fd, &now, sizeof(int)) != 0) {
        if (now % first != 0) {
          write(fds[1], &now, sizeof(int));
        }
      }

      close(fd);
      close(fds[1]);

      wait(&wstat);

      exit(0);
    } else {
      fprintf(2, "pingpong: fork failed\n");
      exit(1);
    }
  }
}