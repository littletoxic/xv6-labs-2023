#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fcntl.h"
#include "kernel/param.h"
#include "user/user.h"

char buf[1024];

int main(int argc, char *argv[]) {
  char *args[MAXARG];
  int appendstart, now;
  int n, m;
  char *p, *q, *s;
  int pid, wstatus;

  if (argc < 2) {
    exit(1);
  }

  for (appendstart = 0; appendstart + 1 < argc; appendstart++) {
    args[appendstart] = argv[appendstart + 1];
  }

  m = 0;
  while ((n = read(0, buf + m, sizeof(buf) - m - 1)) > 0) {
    m += n;
    buf[m] = '\0';
    p = buf;
    while ((q = strchr(p, '\n')) != 0) {
      *q = 0;
      now = appendstart;
      while ((s = strchr(p, ' ')) != 0 && s < q) {
        *s = 0;
        if (now > MAXARG - 2) {
          exit(1);
        }
        args[now++] = p;
        p = s + 1;
      }
      if (now > MAXARG - 2) {
        exit(1);
      }
      args[now++] = p;
      args[now] = 0;

      pid = fork();
      if (pid == 0) {
        exec(args[0], args);
      } else if (pid > 0) {
        wait(&wstatus);
      } else {
        exit(1);
      }
      p = q + 1;
    }
    if (m > 0) {
      m -= p - buf;
      memmove(buf, p, m);
    }
  }

  exit(0);
}