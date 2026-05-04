#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "threads/mmu.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
#include "kernel/stdio.h"

void syscall_entry (void);
void syscall_handler (struct intr_frame *);
static bool is_valid_user_buffer (const void *buffer, size_t size);

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
	uint64_t sys_num = f->R.rax;
	uint64_t arg0 = f->R.rdi;
	uint64_t arg1 = f->R.rsi;
	uint64_t arg2 = f->R.rdx;
	uint64_t arg3 = f->R.r10;
	uint64_t arg4 = f->R.r8;
	uint64_t arg5 = f->R.r9;

	if(sys_num == SYS_WRITE) {
		int fd = (int)arg0; // File descriptor
		const void *buf       = (void *)arg1; // buffer
		size_t buf_size = (size_t)arg2; // size
		// TODO 잘못된 fd가 왔을때 처리 로직
		if(fd <= 0) {
			f->R.rax = -1;
			return;
		}

		if(fd == 1) {
			if (!is_valid_user_buffer(buf, buf_size))
			{
				thread_current()->exit_code = -1;
				thread_exit();
			}

			putbuf(buf, buf_size);
		}

		// TODO 1이 아닌 실제 파일 입력이 들어왔을때 로직 처리

		// 사용한 바이트 수 만큼 리턴
		f->R.rax = buf_size;
	} else if(f->R.rax == SYS_EXIT) {
		thread_current()->exit_code = arg0;
		thread_exit();
	}
}

static bool is_valid_user_buffer (const void *buffer, size_t size) {
	if (size == 0) {
		return true;
	}

	if (buffer == NULL) {
		return false;
	}

	uintptr_t start = (uintptr_t)buffer;
	uintptr_t end = start + size - 1;

	if (end < start) {
		return false;
	}

	if (!is_user_vaddr((const void *)start) || !is_user_vaddr((const void *)end)) {
		return false;
	}

	uintptr_t addr = (uintptr_t)pg_round_down((const void *)start);
	
	for (; addr <= end; addr += PGSIZE) {
		if (pml4_get_page(thread_current()->pml4, (const void *)addr) == NULL) {
			return false;
		}
	}
	
	return true;
}
