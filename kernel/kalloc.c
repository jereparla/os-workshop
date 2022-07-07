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
  int refc[(PHYSTOP - 0x80049000) / PGSIZE];
} kmem;

int get_ref_index(void *pa){
  if((char*) pa < end){
    panic("get ref index");
  }

  char *e = (char*) PGROUNDDOWN((uint64)pa);
  char *s = (char*) PGROUNDUP((uint64)end);
  int index = (e - s) >> PGSHIFT;
  return index; 
}

int get_ref_count(void *pa){
  int index = get_ref_index(pa);
  acquire(&kmem.lock);
  int refs = kmem.refc[index];
  release(&kmem.lock);
  return refs;
}

void add_ref(void *pa){
  int index = get_ref_index(pa);
  acquire(&kmem.lock);
  kmem.refc[index]++;
  release(&kmem.lock);
}

void dec_ref(void *pa){
  int index = get_ref_index(pa);
  int count = get_ref_count(pa);
  if(count <= 0){
    panic("def ref: count cannot be negative");
  }
  acquire(&kmem.lock);
  kmem.refc[index]--;
  if(kmem.refc[index] == 0){
    kfree(pa);
  }
  release(&kmem.lock);
} 

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
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE){
    kfree(p);
    int index = get_ref_index(p);
    acquire(&kmem.lock);
    kmem.refc[index] = 0;
    release(&kmem.lock);
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

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
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
  if(r){
    kmem.freelist = r->next;
    add_ref(r);
  }
  release(&kmem.lock);

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}
