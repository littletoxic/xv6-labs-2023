#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"
#include "kernel/fcntl.h"

int strncmp(const char *p, const char *q, int n) {
  while (*p && *p == *q && n-- > 0)
    p++, q++;
  return n <= 0 ? 0 : (uchar)*p - (uchar)*q;
}

void find(char *path, char *expression, int expressionlen) {
  char buf[512], *p;
  int fd;
  struct dirent de;
  struct stat st;
  int count;

  if ((fd = open(path, O_RDONLY)) < 0) {
    fprintf(2, "ls: cannot open %s\n", path);
    return;
  }

  if (fstat(fd, &st) < 0) {
    fprintf(2, "ls: cannot stat %s\n", path);
    close(fd);
    return;
  }

  if ((count = strlen(path) - expressionlen) >= 0) {
    p = path;
    while (count-- >= 0) {
      if (strncmp(p, expression, expressionlen) == 0) {
        printf("%s\n", path);
        break;
      }
      p++;
    }
  }

  if (st.type == T_DIR) {
    if (strlen(path) + 1 + DIRSIZ + 1 > sizeof buf) {
      printf("find: path too long\n");
      exit(1);
    }
    strcpy(buf, path);
    p = buf + strlen(buf);
    *p++ = '/';
    while (read(fd, &de, sizeof(de)) == sizeof(de)) {
      if (de.inum == 0 || strcmp(de.name, ".") == 0 ||
          strcmp(de.name, "..") == 0)
        continue;
      memmove(p, de.name, DIRSIZ);
      p[DIRSIZ] = 0;
      find(buf, expression, expressionlen);
    }
  }
  close(fd);
}

int main(int argc, char *argv[]) {
  int expressionlen;

  if (argc != 3) {
    fprintf(2, "Usage: find path expression\n");
    exit(1);
  }

  expressionlen = strlen(argv[2]);
  find(argv[1], argv[2], expressionlen);

  exit(0);
}