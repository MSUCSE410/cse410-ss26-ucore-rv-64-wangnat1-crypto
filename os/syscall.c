#include "syscall.h"
#include "defs.h"
#include "loader.h"
#include "syscall_ids.h"
#include "timer.h"
#include "trap.h"
#include "proc.h"
#include "vm.h" 
uint64 sys_write(int fd, uint64 va, uint len)
{
	///saueo
	debugf("sys_write fd = %d va = %x, len = %d", fd, va, len);
	if (fd != STDOUT)
		return -1;
	struct proc *p = curr_proc();
	char str[MAX_STR_LEN];
	int size = copyinstr(p->pagetable, str, va, MIN(len, MAX_STR_LEN));
	debugf("size = %d", size);
	for (int i = 0; i < size; ++i) {
		console_putchar(str[i]);
	}
	return size;
}

__attribute__((noreturn)) void sys_exit(int code)
{
	exit(code);
	__builtin_unreachable();
}

uint64 sys_sched_yield()
{
	yield();
	return 0;
}

uint64 sys_gettimeofday(TimeVal *val, int _tz) // TODO: implement sys_gettimeofday in pagetable. (VA to PA)
{
	// YOUR CODE
	(void)_tz;
	//ignore timezone
	if(val ==0) {
		return -1;
	}
	//check if yser passed null
	//val is a user virtual address 
	struct proc *p = curr_proc();
	uint64 uva = (uint64)val;
	//convert user va to something kernel can access
	
	TimeVal tv;
	uint64 cycle = get_cycle();
	tv.sec = cycle /CPU_FREQ;
	tv.usec = (cycle % CPU_FREQ) * 1000000 / CPU_FREQ;
	//ch3 user code and kernel looking at the same physical mememory directly, so when user passed pointer like val, pointer treated like a physical addr
	//accessed ram at that number
	//ch4, paging/virtual memory is enabled, pointer passed by user like val is a user virtual addr, kernel cant tell difference
	//kernel and user run under different page tables/mappings 
	//if user VA is not map, dereferncing it would cause a page fault,
	//kernel must protect information so user pointers dont cause writing to kernel memory

	
	/* The code in `ch3` will leads to memory bugs*/

	// uint64 cycle = get_cycle();
	// val->sec = cycle / CPU_FREQ;
	// val->usec = (cycle % CPU_FREQ) * 1000000 / CPU_FREQ;

	if (copyout(p->pagetable, uva, (char *)&tv,sizeof(tv))<0){
		return -1;
	}
	return 0;
}

// TODO: add support for mmap and munmap syscall.
// hint: read through docstrings in vm.c. Watching CH4 video may also help.
// Note the return value and PTE flags (especially U,X,W,R)
int sys_mmap(void *start, uint64 len, int port, int flag, int fd){
	//map a new memory region into the current process addr space
	//anonymous mmaping, tkaes vA length and allocates fresh physical pages, in kernel
	(void)flag; 
	(void)fd;
	uint64 va = (uint64)start;
	if(len == 0) {
		return 0;
	}
	//start at user va start, 
	if (len > (1ULL << 30)) {
		return -1;
	}   // cap at 1GiB
    if (va % PGSIZE != 0) {
		return -1;
	}   // page-aligned
    if ((port & ~0x7) != 0) {
		return -1;
	}    // invalid bits, allow low 3 bits
    if ((port & 0x7) == 0) {
		return -1;
	 }   // must request at laest one per perm
	if (va + len < va){
		return -1;
	}
    struct proc *p = curr_proc();

    uint64 end = va + len;
    uint64 a0 = PGROUNDDOWN(va);
    uint64 last = PGROUNDDOWN(end - 1);
	 // compute page range
	 //PTE permission flags
    int perm = PTE_U;
    if (port & 0x1) perm |= PTE_R;
    if (port & 0x2) perm |= PTE_W;
    if (port & 0x4) perm |= PTE_X;

    // overlap check first
    for (uint64 a = a0; a <= last; a += PGSIZE) {
		if (walkaddr(p->pagetable,a)!=0){
			return -1;
		}
			//page mapped already
	}


    // allocate + map each page
    for (uint64 a = a0; a <= last; a += PGSIZE) {
        void *pa = kalloc();
		//physical range
        if (pa == 0){
			return -1;
		}  // no rollback 
        memset(pa, 0, PGSIZE);
        if (mappages(p->pagetable, a, PGSIZE, (uint64)pa, perm) != 0)
            return -1;
		//makes it zero filled, then installs PTE va-a-> PA with flags
    }

    return 0;
}
int sys_munmap(void *start, uint64 len)
{
	//unmpas a range of usa, for current procces, frees physcial pages
    uint64 va = (uint64)start;
    if (len == 0) {
		return 0;
	}
    if (va % PGSIZE != 0) {
		return -1;
	}
	if (va + len < va){
		return -1;
	}
    struct proc *p = curr_proc();


    uint64 a0 = PGROUNDDOWN(va);
    uint64 last = PGROUNDDOWN(va + len - 1);
	//compute page ranges
    // verify fully mapped
    for (uint64 a = a0; a <= last; a += PGSIZE) {
        if (walkaddr(p->pagetable,a) ==0){
			return -1;
			//walkadder, for user mapping aware requires PTE_U,
		}
			//enforces all pr nothing behavior, if any page is unmapped it fails
	}


    uint64 npages = (last - a0) / PGSIZE + 1;
    uvmunmap(p->pagetable, a0, npages, 1);
	//removes PTEs and frees the pyhsical pages
	sfence_vma();
	//flushes stale TLB entreis so CPU wont keep using old translations
    return 0;
} 

/*
* LAB1: you may need to define sys_task_info here
*/

uint64 sys_getpid(){
	return curr_proc()->pid;
}//was getting an error with syscall 172, and decided to call sys_getPID

uint64 sys_task_info(TaskInfo *ti_uva)
//call sys_task_info
//returns status, syscall_times[] array. time in ms first scheduled
{
	if (ti_uva == 0){
		return -1;
	}
	//checks pointer is null, 
	struct proc *p = curr_proc();
	uint64 uva = (uint64)ti_uva;
	//get current proccess
	TaskInfo kti;
	kti.status = 2;
	//the test expects TaskInfo.status, to follow the user-space TaskStatus enum
	//Uninit = 0, READY =1, RUNNING=2, EXITED=3, and errors faced I set TaskStatus=2;RUnning

	for(int i = 0; i < MAX_SYSCALL_NUM; i++){
		kti.syscall_times[i] = p->syscall_times[i];
	}
	//copy the per-process syscall counter array;ID =syscall ID
	//sees how many times each syscall has been called by the process
	uint64 now = get_cycle();
	if (p->start_cycle == 0){
		kti.time = 0;
	} else{
		kti.time = (int)((now-p->start_cycle)*1000/CPU_FREQ);
	}
	if (copyout(p->pagetable,uva,(char *)&kti, sizeof(kti))<0){
		return -1;
	}
	//Calculate runtime, get current CPU cycle count,
	//if start_cycle, checks whether the task has ever been scheduled, 
	//if not, process created but not executed, no, vlaid starting time, so runtime is 0
	//else, task has run before; compute run time by the # of cycles that existed, multiply 1000 and divied by CPU_freq to get ms

	return 0;
}


extern char trap_page[];

void syscall()
{
	struct trapframe *trapframe = curr_proc()->trapframe;
	int id = trapframe->a7, ret;
	uint64 args[6] = { trapframe->a0, trapframe->a1, trapframe->a2,
			   trapframe->a3, trapframe->a4, trapframe->a5 };
	tracef("syscall %d args = [%x, %x, %x, %x, %x, %x]", id, args[0],
	       args[1], args[2], args[3], args[4], args[5]);
	/*
	* LAB1: you may need to update syscall counter for task info here
	*/
	if (id>=0 && id< MAX_SYSCALL_NUM){
		curr_proc()->syscall_times[id]++;
	} // update, syscall counter, incrment the counter at index, checks in place to prevent out of bounds indexing, if invalid syscall number

	//switches
	//select correct kernel function based on syscall number
	switch (id) {
	case SYS_write:
		ret = sys_write(args[0], args[1], args[2]);
		break;
	case SYS_exit:
		sys_exit(args[0]);
		// __builtin_unreachable();
	case SYS_sched_yield:
		ret = sys_sched_yield();
		break;
	case SYS_gettimeofday:
		ret = sys_gettimeofday((TimeVal *)args[0], args[1]);
		break;
	/*
	* LAB1: you may need to add SYS_taskinfo case here
	*/
	case SYS_task_info:
	ret = sys_task_info((TaskInfo *)args[0]);
	break;
	//user pointer to TaskInfo struct, fills with status, syscall usage, runtime
	case SYS_getpid:
	ret = sys_getpid();
	break;
	//added because of error with syscall 172, to prevent unimplemented syscall
	case SYS_mmap:
	ret = sys_mmap((void *)args[0], (uint64)args[1], (int)args[2], (int)args[3], (int)args[4]);
    break;
		// start[0], len[1],[2] port, [3] flag, [4] fd, 
	
	case SYS_munmap:
    ret = sys_munmap((void *)args[0], (uint64)args[1]);
    break;

	default:
		ret = -1;
		errorf("unknown syscall %d", id);
	}
	trapframe->a0 = ret;
	tracef("syscall ret %d", ret);
}
