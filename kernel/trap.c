#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

struct spinlock tickslock;
uint ticks;

extern char trampoline[], uservec[], userret[];

// in kernelvec.S, calls kerneltrap().
void kernelvec();

extern int devintr();
extern void ageprocs();

void
trapinit(void)
{
  initlock(&tickslock, "time");
}

// set up to take exceptions and traps while in the kernel.
void
trapinithart(void)
{
  w_stvec((uint64)kernelvec);
}

//
// handle an interrupt, exception, or system call from user space.
// called from trampoline.S
//
void
usertrap(void)
{
  int which_dev = 0;


  if((r_sstatus() & SSTATUS_SPP) != 0)
    panic("usertrap: not from user mode");

  // send interrupts and exceptions to kerneltrap(),
  // since we're now in the kernel.
  w_stvec((uint64)kernelvec);

  struct proc *p = myproc();
  
  // save user program counter.
  p->trapframe->epc = r_sepc();
  
  if(r_scause() == 8){
    // system call

    if(p->killed)
      exit(-1);

    // sepc points to the ecall instruction,
    // but we want to return to the next instruction.
    p->trapframe->epc += 4;

    // an interrupt will change sstatus &c registers,
    // so don't enable until done with those registers.
    intr_on();

    syscall();
    //scause = 15 and scause = 13 indicate a page fault (per risc-v man).
  } else if (r_scause() == 12 || r_scause() == 2 || r_scause() == 13 || r_scause() == 15){
    
    uint64 sterror = r_stval();
    uint flags = get_flags(p->pagetable, sterror);
    int f_w = ((flags) & PTE_W);
    int f_v = ((flags) & PTE_V);
    
    if(f_w == 0 && f_v != 0){
      printf("ENTRA POR FLAG APAGADO Y EL VALID ES %x \n ", flags);
      uint64 pa = get_pa(p->pagetable, sterror);
      uint refc = get_refc(pa);
      if (refc > 1) {
        char* mem;
        if((mem = kalloc()) == 0){
          printf("FALLO EL KALLOC");
          // goto err;
        }
        memmove(mem, (char*)pa, PGSIZE);
        printf("LLAMA AL UNMAP \n");
        printf(" el va es %x \n", sterror);
        printf(" la PA ES %x \n", pa);
        printf(" los flags son  %x \n", flags);
        printf(" el process id es  %s \n", p->name);
        uvmunmap(p->pagetable, PGROUNDDOWN(sterror), 1, 0);
        if(mappages(p->pagetable, sterror, 1, (uint64)mem, PTE_W|PTE_R|PTE_X|PTE_U) != 0){
          printf("FALLO EL map pages");
          // goto err;
        }
        decrement_refc(pa);
        pa = get_pa(p->pagetable, sterror);
        flags = get_flags(p->pagetable, sterror);
        printf("DESPUES DEL MAP \n");
        printf(" la PA ES %x \n", pa);
        printf(" los flags son  %x \n", flags);
        printf(" el process id es  %s \n", p->name);
      } else if (refc == 1) {
        printf("COUNT ES 1");
        setw(p->pagetable, sterror);
      } else {
        panic("wrong reference count");
      }
    }
    else if(f_v == 0){
      printf("ENTRA POR FLAG PRENDIDO Y EL VALID ES %x \n ", (flags));
      // this is true if trying to access an address either in the free page or above the program's code+data
      // or below the stack
      if (sterror < p->cdsize || sterror > p->cdsize + MAXSTACKPGS * PGSIZE) {
        printf(" el process name es  %s \n", p->name);
        printf("usertrap(): page fault %p pid=%d\n", r_scause(), p->pid);
        printf("            sepc=%p stval=%p\n", r_sepc(), r_stval());
        p->killed = 1;
      }
      // this is true if trying to access one of the free pages available for stack expansion
      else {
        printf("There would be a page fault at: %x \n", sterror);
        uint64 new_stack_block = PGROUNDDOWN(sterror);
        uint64 sz1;
        printf("But we assign a new stack block at: %x \n", new_stack_block);
        if((sz1 = uvmalloc(p->pagetable, new_stack_block, new_stack_block + PGSIZE)) == 0)
          panic("uvmalloc");
        printf("New stack block + PGSIZE: %x \n", sz1);
      }
      printf("SALE DEL FLAG PRENDIDO \n");
    }
    else{
      printf("usertrap(): unexpected scause %p pid=%d\n", r_scause(), p->pid);
      printf("            sepc=%p stval=%p\n", r_sepc(), r_stval());
      p->killed = 1;
    }
  } else if((which_dev = devintr()) != 0){
    // ok
  } else {
    printf("usertrap(): unexpected scause %p pid=%d\n", r_scause(), p->pid);
    printf("            sepc=%p stval=%p\n", r_sepc(), r_stval());
    p->killed = 1;
  }

  if(p->killed)
    exit(-1);

  // give up the CPU if this is a timer interrupt.
  if(which_dev == 2){

    // increment the tick counter each time there is a timer interrupt, and
    // yield if it reaches the quantum.
    if((++myproc()->ticks) == QUANTUM){
      /* printf("Process %d abandoned the CPU %d (USER CONTEXT) \n", myproc()->pid, cpuid()); */
      yield();
    }
  }

  usertrapret();
}

//
// return to user space
//
void
usertrapret(void)
{
  struct proc *p = myproc();

  // we're about to switch the destination of traps from
  // kerneltrap() to usertrap(), so turn off interrupts until
  // we're back in user space, where usertrap() is correct.
  intr_off();

  // send syscalls, interrupts, and exceptions to trampoline.S
  w_stvec(TRAMPOLINE + (uservec - trampoline));

  // set up trapframe values that uservec will need when
  // the process next re-enters the kernel.
  p->trapframe->kernel_satp = r_satp();         // kernel page table
  p->trapframe->kernel_sp = p->kstack + PGSIZE; // process's kernel stack
  p->trapframe->kernel_trap = (uint64)usertrap;
  p->trapframe->kernel_hartid = r_tp();         // hartid for cpuid()

  // set up the registers that trampoline.S's sret will use
  // to get to user space.
  
  // set S Previous Privilege mode to User.
  unsigned long x = r_sstatus();
  x &= ~SSTATUS_SPP; // clear SPP to 0 for user mode
  x |= SSTATUS_SPIE; // enable interrupts in user mode
  w_sstatus(x);

  // set S Exception Program Counter to the saved user pc.
  w_sepc(p->trapframe->epc);

  // tell trampoline.S the user page table to switch to.
  uint64 satp = MAKE_SATP(p->pagetable);

  // jump to trampoline.S at the top of memory, which 
  // switches to the user page table, restores user registers,
  // and switches to user mode with sret.
  uint64 fn = TRAMPOLINE + (userret - trampoline);
  ((void (*)(uint64,uint64))fn)(TRAPFRAME, satp);
}

// interrupts and exceptions from kernel code go here via kernelvec,
// on whatever the current kernel stack is.
void 
kerneltrap()
{
  int which_dev = 0;
  uint64 sepc = r_sepc();
  uint64 sstatus = r_sstatus();
  uint64 scause = r_scause();
  
  if((sstatus & SSTATUS_SPP) == 0)
    panic("kerneltrap: not from supervisor mode");
  if(intr_get() != 0)
    panic("kerneltrap: interrupts enabled");

  if((which_dev = devintr()) == 0){
    printf("scause %p\n", scause);
    printf("sepc=%p stval=%p\n", r_sepc(), r_stval());
    panic("kerneltrap");
  }

  // give up the CPU if this is a timer interrupt.
  if(which_dev == 2 && myproc() != 0 && myproc()->state == RUNNING){

    // increment the tick counter each time there is a timer interrupt, and
    // yield if it reaches the quantum.
    if((++myproc()->ticks) == QUANTUM){
      /* printf("Process %d abandoned the CPU %d (KERNEL CONTEXT) \n", myproc()->pid, cpuid()); */
      yield();
    }
  }

  // the yield() may have caused some traps to occur,
  // so restore trap registers for use by kernelvec.S's sepc instruction.
  w_sepc(sepc);
  w_sstatus(sstatus);
}

void
clockintr()
{
  acquire(&tickslock);
  ticks++;
  if(ticks % TIMEUNIT == 0) { 
    ageprocs(); 
  } 
  wakeup(&ticks);
  release(&tickslock);
}

// check if it's an external interrupt or software interrupt,
// and handle it.
// returns 2 if timer interrupt,
// 1 if other device,
// 0 if not recognized.
int
devintr()
{
  uint64 scause = r_scause();

  if((scause & 0x8000000000000000L) &&
     (scause & 0xff) == 9){
    // this is a supervisor external interrupt, via PLIC.

    // irq indicates which device interrupted.
    int irq = plic_claim();

    if(irq == UART0_IRQ){
      uartintr();
    } else if(irq == VIRTIO0_IRQ){
      virtio_disk_intr();
    } else if(irq){
      printf("unexpected interrupt irq=%d\n", irq);
    }

    // the PLIC allows each device to raise at most one
    // interrupt at a time; tell the PLIC the device is
    // now allowed to interrupt again.
    if(irq)
      plic_complete(irq);

    return 1;
  } else if(scause == 0x8000000000000001L){
    // software interrupt from a machine-mode timer interrupt,
    // forwarded by timervec in kernelvec.S.

    if(cpuid() == 0){
      clockintr();
    }
    
    // acknowledge the software interrupt by clearing
    // the SSIP bit in sip.
    w_sip(r_sip() & ~2);

    return 2;
  } else {
    return 0;
  }
}

