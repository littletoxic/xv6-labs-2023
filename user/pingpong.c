#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fcntl.h"
#include "user/user.h"

int main(int argc, char *argv[]) {
  int pid;
  int fdctp[2];
  int fdptc[2];
  char c;

  if (pipe(fdctp) < 0) {
    fprintf(2, "pingpong: pipe failed\n");
    exit(1);
  }
  if (pipe(fdptc) < 0) {
    fprintf(2, "pingpong: pipe failed\n");
    exit(1);
  }
  pid = fork();
  if (pid == 0) {
    // child
    close(fdctp[0]);
    close(fdptc[1]);

    read(fdptc[0], &c, 1);
    close(fdptc[0]);
    printf("%d: received ping\n", getpid());

    write(fdctp[1], "", 1);
    close(fdctp[1]);

    exit(0);
  } else if (pid > 0) {
    // parent
    close(fdctp[1]);
    close(fdptc[0]);

    write(fdptc[1], "", 1);
    close(fdptc[1]);

    read(fdctp[0], &c, 1);
    close(fdctp[0]);
    printf("%d: received pong\n", getpid());

    exit(0);
  } else {
    fprintf(2, "pingpong: fork failed\n");
    exit(1);
  }
}