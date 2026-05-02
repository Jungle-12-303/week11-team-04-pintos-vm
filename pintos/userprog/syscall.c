#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"

void syscall_entry (void);
void syscall_handler (struct intr_frame *);

/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081         /* Segment selector msr */
#define MSR_LSTAR 0xc0000082        /* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */

void
syscall_init (void) {
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48  |
			((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t) syscall_entry);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
}

/* The main system call interface */
void
syscall_handler (struct intr_frame *f UNUSED) {
	// TODO: Your implementation goes here.
	// printf ("system call!\n");
	if(f->R.rax == SYS_WRITE) {
		uint64_t fd    = (uint64_t )f->R.rdi; // File descriptor
		void *arg0 = (void *)f->R.rsi; // buffer
		void *arg1 = (void *)f->R.rdx; // size
		for(int i = 0; i < (size_t)arg1; i++) {
			printf("%c", *((char *)arg0 + i));
		}
		f->R.rax = 0;
	} else if(f->R.rax == SYS_EXIT) {
		int ret = (int)f->R.rdi;
		// FIXME: 프로그램 이름으로 해야하는데 임시로 thread_name으로 넣어둠.
		// exit에 대한 다른 방법을 사용해야함. lib.c의 vmsg 함수 안 주석 참고
		printf("%s: exit(%d)\n", thread_name(),ret);
		thread_exit();
	}
}
