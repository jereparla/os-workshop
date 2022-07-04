// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
  uint refc[PHYSTOP >> PGSHIFT];
} kmem;

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE) {
    kmem.refc[(uint64)p >> PGSHIFT] = 0;
    kfree(p);
  }
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  r = (struct run*)pa;

  // if ref count is greater than 0, decrease it
  if(kmem.refc[(uint64)pa >> PGSHIFT] > 0) {
    --kmem.refc[(uint64)pa >> PGSHIFT];
  }

  // if ref count is 0, then do the regular kfree
  if(kmem.refc[(uint64)pa >> PGSHIFT] == 0){

    // Fill with junk to catch dangling refs.
    memset(pa, 1, PGSIZE);


    acquire(&kmem.lock);
    r->next = kmem.freelist;
    kmem.freelist = r;
    release(&kmem.lock);
  }
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;

  // if memory can be allocated, do so and give it a ref count of 1
  if(r) {
    kmem.freelist = r->next;
    kmem.refc[(uint64)((char*)r) >> PGSHIFT] = 1;
  }

  release(&kmem.lock);

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}

void
decrement_refc(uint64 pa)
{
  if(pa < (uint64)end || pa >= PHYSTOP)
    panic("decrement_refc");

  acquire(&kmem.lock);
  --kmem.refc[pa >> PGSHIFT];
  release(&kmem.lock);
}

void
increment_refc(uint64 pa)
{
  if(pa < (uint64)end || pa >= PHYSTOP)
    panic("increment_refc");

  acquire(&kmem.lock);
  ++kmem.refc[pa >> PGSHIFT];
  release(&kmem.lock);
}

int
get_refc(uint64 pa)
{
  if(pa < (uint64)end || pa >= PHYSTOP)
    panic("get_refc");

  uint count;
  acquire(&kmem.lock);
  count = kmem.refc[pa >> PGSHIFT];
  release(&kmem.lock);

  return count;
}
