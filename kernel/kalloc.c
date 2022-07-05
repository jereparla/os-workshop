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
  uint refc[(PHYSTOP - 0x80049000) / PGSIZE];
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
  // printf("EL END ES: %p ", (uint64) end);
  // printf("EL PHYSTOP ES: %p ", (uint64) PHYSTOP);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE) {
    kmem.refc[((uint64)p - (uint64) end)>> PGSHIFT] = 0;
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
  // printf("ENTRA AL KFREE \n");
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  r = (struct run*)pa;

  // if ref count is greater than 0, decrease it
  if(kmem.refc[((uint64)pa - (uint64)end)>> PGSHIFT] > 0) {
    // printf("DECREMENTA LA PA %p \n", (uint64)pa - (uint64)end);
    --kmem.refc[((uint64)pa - (uint64)end) >> PGSHIFT];
  }

  // if ref count is 0, then do the regular kfree
  if(kmem.refc[((uint64)pa - (uint64)end) >> PGSHIFT] == 0){
    // printf("HACE EL MEMSET DE LA PA %p \n", (uint64)pa - (uint64)end);
    // Fill with junk to catch dangling refs.
    memset(pa, 1, PGSIZE);


    acquire(&kmem.lock);
    r->next = kmem.freelist;
    kmem.freelist = r;
    release(&kmem.lock);
  }
  // printf("SALE DEL KFREE \n");
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  // printf("ENTRA AL KALLOC \n");
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;

  // if memory can be allocated, do so and give it a ref count of 1
  if(r) {
    kmem.freelist = r->next;
    kmem.refc[((uint64)((char*)r)- (uint64)end) >> PGSHIFT] = 1;
    // printf("INICIA LA PA %p CON 1 \n", (uint64)((char*)r)- (uint64)end);
  }

  release(&kmem.lock);

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  // printf("SALE DEL KALLOC \n");
  return (void*)r;
}

void
decrement_refc(uint64 pa)
{
  // printf("ENTRA AL DECREMENT REF \n");
  if(pa < (uint64)end || pa >= PHYSTOP)
    panic("decrement_refc");

  acquire(&kmem.lock);
  // printf("DECREMENTA LA PA %p \n", (pa - (uint64) end));
  --kmem.refc[(pa - (uint64) end) >> PGSHIFT];
  release(&kmem.lock);
  // printf("SALE DEL DECREMENT REF \n");
}

void
increment_refc(uint64 pa)
{
  // printf("ENTRA AL INCREMENT REF \n");
  if(pa < (uint64)end || pa >= PHYSTOP)
    panic("increment_refc");

  acquire(&kmem.lock);
  // printf("INCREMENTA LA PA %p \n", (pa - (uint64) end));
  ++kmem.refc[(pa - (uint64) end)>> PGSHIFT];
  release(&kmem.lock);
  // printf("SALE DEL INCREMENT REF \n");
}

int
get_refc(uint64 pa)
{
  // printf("ENTRA AL GET REF \n");
  if(pa < (uint64)end || pa >= PHYSTOP)
    panic("get_refc");

  uint count;
  acquire(&kmem.lock);
  count = kmem.refc[(pa - (uint64) end) >> PGSHIFT];
  // printf("OBTIENE EL COUNT DE LA PA %p \n", (pa - (uint64) end));

  release(&kmem.lock);
  // printf("SALE DEL GET REF \n");
  return count;
}
