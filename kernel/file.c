//
// Support functions for system calls that involve file descriptors.
//

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "fs.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "file.h"
#include "stat.h"
#include "proc.h"
#include "fcntl.h"

struct devsw devsw[NDEV];
struct {
  struct spinlock lock;
  struct file file[NFILE];
} ftable;

void
fileinit(void)
{
  initlock(&ftable.lock, "ftable");
}

// Allocate a file structure.
struct file*
filealloc(void)
{
  struct file *f;

  acquire(&ftable.lock);
  for(f = ftable.file; f < ftable.file + NFILE; f++){
    if(f->ref == 0){
      f->ref = 1;
      release(&ftable.lock);
      return f;
    }
  }
  release(&ftable.lock);
  return 0;
}

// Increment ref count for file f.
struct file*
filedup(struct file *f)
{
  acquire(&ftable.lock);
  if(f->ref < 1)
    panic("filedup");
  f->ref++;
  release(&ftable.lock);
  return f;
}

// Close file f.  (Decrement ref count, close when reaches 0.)
void
fileclose(struct file *f)
{
  struct file ff;

  acquire(&ftable.lock);
  if(f->ref < 1)
    panic("fileclose");
  if(--f->ref > 0){
    release(&ftable.lock);
    return;
  }
  ff = *f;
  f->ref = 0;
  f->type = FD_NONE;
  release(&ftable.lock);

  if(ff.type == FD_PIPE){
    pipeclose(ff.pipe, ff.writable);
  } else if(ff.type == FD_INODE || ff.type == FD_DEVICE){
    begin_op();
    iput(ff.ip);
    end_op();
  }
}

// Get metadata about file f.
// addr is a user virtual address, pointing to a struct stat.
int
filestat(struct file *f, uint64 addr)
{
  struct proc *p = myproc();
  struct stat st;
  
  if(f->type == FD_INODE || f->type == FD_DEVICE){
    ilock(f->ip);
    stati(f->ip, &st);
    iunlock(f->ip);
    if(copyout(p->pagetable, addr, (char *)&st, sizeof(st)) < 0)
      return -1;
    return 0;
  }
  return -1;
}

// Read from file f.
// addr is a user virtual address.
int
fileread(struct file *f, uint64 addr, int n)
{
  int r = 0;

  if(f->readable == 0)
    return -1;

  if(f->type == FD_PIPE){
    r = piperead(f->pipe, addr, n);
  } else if(f->type == FD_DEVICE){
    if(f->major < 0 || f->major >= NDEV || !devsw[f->major].read)
      return -1;
    r = devsw[f->major].read(1, addr, n);
  } else if(f->type == FD_INODE){
    ilock(f->ip);
    if((r = readi(f->ip, 1, addr, f->off, n)) > 0)
      f->off += r;
    iunlock(f->ip);
  } else {
    panic("fileread");
  }

  return r;
}

// Write to file f.
// addr is a user virtual address.
int
filewrite(struct file *f, uint64 addr, int n)
{
  int r, ret = 0;

  if(f->writable == 0)
    return -1;

  if(f->type == FD_PIPE){
    ret = pipewrite(f->pipe, addr, n);
  } else if(f->type == FD_DEVICE){
    if(f->major < 0 || f->major >= NDEV || !devsw[f->major].write)
      return -1;
    ret = devsw[f->major].write(1, addr, n);
  } else if(f->type == FD_INODE){
    // write a few blocks at a time to avoid exceeding
    // the maximum log transaction size, including
    // i-node, indirect block, allocation blocks,
    // and 2 blocks of slop for non-aligned writes.
    // this really belongs lower down, since writei()
    // might be writing a device like the console.
    int max = ((MAXOPBLOCKS-1-1-2) / 2) * BSIZE;
    int i = 0;
    while(i < n){
      int n1 = n - i;
      if(n1 > max)
        n1 = max;

      begin_op();
      ilock(f->ip);
      if ((r = writei(f->ip, 1, addr + i, f->off, n1)) > 0)
        f->off += r;
      iunlock(f->ip);
      end_op();

      if(r != n1){
        // error from writei
        break;
      }
      i += r;
    }
    ret = (i == n ? n : -1);
  } else {
    panic("filewrite");
  }

  return ret;
}

void
mmapfile(pte_t *pte, uint64 va)
{
  int i, perm;
  char *mem;
  struct file *f;
  struct vma *vma = 0;
  struct proc *p = myproc();

  for (i = 0; i < 16; i++) {
    if (!p->vmas[i].valid)
      continue;

    if (va >= p->vmas[i].va && va < p->vmas[i].end) {
      vma = &p->vmas[i];
      break;
    }
  }

  if (vma == 0) {
    panic("handlemmap: pte flag exist but no map");
  }

  if ((mem = kalloc()) == 0)
    panic("handlemmap: kalloc");

  f = vma->f;
  // assume that prot is PROT_READ or PROT_WRITE or both
  perm = 0;
  if (f->readable != 0 && vma->prot & PROT_READ)
    perm |= PTE_R;
  if (vma->prot & PROT_WRITE && (vma->flags & MAP_PRIVATE || f->writable != 0))
    perm |= PTE_W;
  *pte = PA2PTE(mem) | perm | PTE_U | PTE_V;

  memset(mem, 0, PGSIZE);
  ilock(f->ip);
  // assume in bound
  readi(f->ip, 0, (uint64)mem, vma->off + va - vma->va, PGSIZE);
  iunlock(f->ip);
}

void munmap(struct vma *vma, uint64 len, uint64 addr, int full, int start) {
  int i = 0;
  pte_t *pte;
  struct proc *p = myproc();

  // write back
  if (vma->f->writable && vma->flags & 0x01 && vma->prot & 0x02) {
    int len0 = len, r;
    // filewrite use f->off so cannot replace
    if (len0 > (int)(vma->f->ip->size - addr + vma->va - vma->off))
      len0 = (int)(vma->f->ip->size - addr + vma->va - vma->off);
    struct file *f = vma->f;
    int max = ((MAXOPBLOCKS-1-1-2) / 2) * BSIZE;
    while(i < len0){
      int len1 = len0 - i;
      if(len1 > max)
        len1 = max;

      begin_op();
      ilock(f->ip);
      r = writei(f->ip, 1, addr + i, vma->off + addr - vma->va + i, len1);

      iunlock(f->ip);
      end_op();

      if(r != len1) {
        panic("munmap: write back");
      }
      i += r;
    }
  }


  if (full) {
    vma->valid = 0;
    fileclose(vma->f);
  } else if (start) {
    vma->va += len;
    vma->off += len;
  } else {
    vma->end = addr;
  }

  for (i = 0; i < len / 4096; i++) {
    if((pte = walk(p->pagetable, addr + i * PGSIZE, 0)) == 0)
      panic("munmap");
    if (*pte & PTE_V)
      kfree((void *)PTE2PA(*pte));
    *pte = 0;
  }
}
