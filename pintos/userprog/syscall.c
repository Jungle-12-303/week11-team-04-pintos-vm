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
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "filesys/directory.h"
#include "userprog/fd.h"
#include "threads/malloc.h"
#include "threads/vaddr.h"
#include "threads/synch.h"
#include "threads/init.h"
#include "lib/user/syscall.h"
#include "userprog/process.h"
#include "userprog/process_child.h"
#include "threads/pte.h"

void syscall_entry (void);
void syscall_handler (struct intr_frame *);

static void check_user_addr (const void *addr);
static void check_user_laddr (const void *buf, const size_t size);
static void check_user_waddr (const void *buf, const size_t size);
static bool is_valid_user_buffer (const void *buffer, size_t size);
static bool check_file_name (const char *s);
static struct file *get_file_from_fd (int fd);

static struct lock filesys_lock;

static void 	syscall_exit (const int exit_code);
static int 		syscall_open (const char *file);
static int 		syscall_write (int fd, const void *buffer, unsigned size);
static pid_t 	syscall_fork (const char *thread_name, struct intr_frame *f);
static int 		syscall_wait(pid_t pid);
static bool 	syscall_remove (const char *file);
static void 	syscall_seek (int fd, unsigned position);
static unsigned syscall_tell (int fd);
static int 		syscall_dup2 (int oldfd, int newfd);
static int 		syscall_read (int fd, const void *buffer, unsigned size);
static int 		syscall_filesize (const int fd);
static int 		syscall_exec (char *file);
static void *   syscall_mmap (void *addr, size_t length, int writable, int fd, off_t offset);
static void 	sysccall_munmap (void *addr);


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
	lock_init(&filesys_lock);
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
	uint64_t sys_num = f->R.rax; // 커널 진입시, 호출된 시스템콜이 무엇인지 확인하는 번호
	uint64_t arg0 = f->R.rdi;
	uint64_t arg1 = f->R.rsi;
	uint64_t arg2 = f->R.rdx;
	uint64_t arg3 = f->R.r10;
	uint64_t arg4 = f->R.r8;
	uint64_t arg5 = f->R.r9;

	switch (sys_num)
	{
	case SYS_WRITE:{
		int fd     = (int)arg0; // File descriptor
		const void *buf       = (void *)arg1; // buffer
		size_t buf_size = (size_t)arg2; // size

		struct fd_entry *entry = fd_get_entry (thread_current ()->fd_table, fd);
		if(fd == 1 && entry != NULL && entry->type == FD_STDOUT) {
			if (!is_valid_user_buffer(buf, buf_size))
			{
				thread_current()->exit_code = -1;
				thread_exit();
			}

			putbuf(buf, buf_size);
			// 시스템콜 처리가 끝날 때는 같은 rax를 반환값 저장용으로 사용
			f->R.rax = buf_size; // 사용한 바이트 수 만큼 리턴
		}
		else{
			/**
			 * 아무 값도 안 넣으면, rax에 기존 값이 그대로 남아 있어서
			 * 사용자 프로그램은 write()의 반환값으로 시스템콜 번호 같은 
			 * 엉뚱한 값을 받기 때문에 성공/실패 유무를 알 수 없고,
			 * 기존값이 양수라면 성공했다고 오인 가능성이 있음
			*/
			// f->R.rax = -1; // 실패시 -1 리턴
			f->R.rax = syscall_write((int)arg0, (const char *)arg1, (unsigned)arg2);
		}
		break;
		}
	case SYS_READ: {
		int fd = (int) arg0;             // File descriptor
		const void *buf = (void *) arg1; // buffer
		size_t buf_size = (size_t) arg2; // size

		struct fd_entry *entry = fd_get_entry (thread_current ()->fd_table, fd);
		if (fd == 0 && entry != NULL && entry->type == FD_STDIN) {
			if (!is_valid_user_buffer (buf, buf_size)) {
				thread_current ()->exit_code = -1;
				thread_exit ();
			}
			for (int i = 0; i < buf_size; i++) {
				*(((char *) buf) + i) = input_getc ();
			}

			// 시스템콜 처리가 끝날 때는 같은 rax를 반환값 저장용으로 사용
			f->R.rax = buf_size; // 사용한 바이트 수 만큼 리턴
		} else {
			/**
			 * 아무 값도 안 넣으면, rax에 기존 값이 그대로 남아 있어서
			 * 사용자 프로그램은 write()의 반환값으로 시스템콜 번호 같은
			 * 엉뚱한 값을 받기 때문에 성공/실패 유무를 알 수 없고,
			 * 기존값이 양수라면 성공했다고 오인 가능성이 있음
			 */
			// f->R.rax = -1; // 실패시 -1 리턴
			f->R.rax = syscall_read ((int) arg0, (const char *) arg1, (unsigned) arg2);
		}
		break;
	}
	case SYS_OPEN:{
		f->R.rax = syscall_open((const char *)arg0);
		break;
	}
	case SYS_HALT:{
		power_off();
		break;
	}
	case SYS_CREATE:{
		const char* file      = (const char*)arg0; // file
		unsigned initial_size = (unsigned)arg1;    // initial_size
		if (!check_file_name (file)) {
			f->R.rax = false;
			break;
		}
		lock_acquire(&filesys_lock);
		bool success = filesys_create(file, initial_size);
		lock_release(&filesys_lock);
		f->R.rax = success;
		break;
	}
	case SYS_REMOVE:{
		f->R.rax = syscall_remove ((const char *) arg0);
		break;
	}
	case SYS_CLOSE:{
		int fd = (int)arg0;
		struct fd_table* curr_th_tb = thread_current()->fd_table;
		struct fd_entry* fde = fd_get_entry(curr_th_tb, fd);

		// fd가 stdin, stdout, 배열 범위를 벗어날 경우
		if(fd <= 1){
			f->R.rax = -1;
			break;
		}

		// fd_entry가 NULL일 때
		if(fde == NULL){
			f->R.rax = -1;
			break;
		}

		fd_entry_free(curr_th_tb , fd);
		f->R.rax = 0;

		break;
	}
	case SYS_FORK:{
		const char* name      = (const char*)arg0; // name
		if (!check_file_name (name)) {
			f->R.rax = false;
			break;
		}
		
		f->R.rax = syscall_fork(name, f);

		break;
	}
	case SYS_WAIT:{
		pid_t pid = (pid_t)arg0;

		f->R.rax = syscall_wait(pid);

		break;
	}
	case SYS_EXIT:{
		syscall_exit(arg0);
		break;
	}
	case SYS_FILESIZE: {
		// return syscall1 (SYS_FILESIZE, fd)
		f->R.rax = syscall_filesize (arg0);
		break;
	}
	case SYS_SEEK: {
		syscall_seek ((int) arg0, (unsigned) arg1);
		break;
	}
	case SYS_TELL: {
		f->R.rax = syscall_tell ((int) arg0);
		break;
	}
	case SYS_EXEC: {
		f->R.rax = syscall_exec((char *) arg0);
		break;
	}
	case SYS_DUP2: {
		f->R.rax = syscall_dup2 ((int) arg0, (int) arg1);
		break;
	}
	case SYS_MMAP: {
		f->R.rax = syscall_mmap((void *) arg0, (size_t) arg1, (int) arg2, (int) arg3, (off_t) arg4);
		break;
	}
	case SYS_MUNMAP: {
		sysccall_munmap((void *) arg0);
		break;
	}
	default:{
		break;
	}
	}
}

static struct file *
get_file_from_fd (int fd) {
	struct fd_table *fdt = thread_current ()->fd_table;
	struct fd_entry *fde;

	if (fd < 2 || fdt == NULL) {
		return NULL;
	}
	fde = fd_get_entry (fdt, fd);
	if (fde == NULL || fde->type != FD_FILE) {
		return NULL;
	}
	return fde->file;
}

static bool
check_file_name (const char *s) {
    for (int i = 0; i <= NAME_MAX; i++) {
        check_user_addr (s + i);

        if (s[i] == '\0')
            return true;
    }

    return false;
}

/* 최대 SIZE byte만큼 주소가 유효한지 검사합니다. 여러 페이지에 걸쳐서 
   있을 경우를 검사하기 위해 페이지의 시작 주소가 유효한지 검사합니다. */
static void
check_user_laddr (const void *buf, const size_t size) {
	const char *start = buf;
	const char *end = start + size;
	if(size == 0) return;
	for(uint64_t p = pg_round_down(start); p < end; p += PGSIZE) {
		check_user_addr(p);
	}
	check_user_addr(end - 1);
}

static void
check_user_waddr (const void *buf, const size_t size) {
	const char *start = buf;
	const char *end = start + size;

	if (size == 0) {
		return;
	}
	check_user_laddr (buf, size);
	for (uint64_t p = (uint64_t) pg_round_down (start); p < (uint64_t) end; p += PGSIZE) {
		uint64_t *pte = pml4e_walk (thread_current ()->pml4, p, 0);
		if (pte == NULL || !(*pte & PTE_W)) {
			syscall_exit (-1);
		}
	}
}

/* 포인터가 실제로 접근 가능한지 검사합니다. 
   실패하면 thread_exit()을 하고 -1을 리턴 합니다. */
static void
check_user_addr (const void *addr) {
	if(addr == NULL || !is_user_vaddr(addr) || pml4_get_page(thread_current ()->pml4, addr) == NULL) {
		syscall_exit(-1);
	}
}

static bool 
is_valid_user_buffer (const void *buffer, size_t size) {
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

// 에러 있으면 true 반환 없으면 false 반환
bool fd_duplicate(struct thread *parent, struct thread *child) { 
    struct fd_table *pfdt = parent->fd_table;
    if (pfdt == NULL) {
        return false;
    }
    child->fd_table = fd_table_init ();
    struct fd_table *cfdt = child->fd_table;
    if (cfdt == NULL) {
        return true;
    }
    struct fd_entry *pfde=NULL;
    struct fd_entry *cfde=NULL;
    for (size_t fd = 0; fd < pfdt->size; fd++) {
        if (fd_is_valid(pfdt,fd)) {
            if (cfdt->size <= fd) {
                while (cfdt->size <= fd) {
                    if (fd_expaned(cfdt,cfdt->size<<1) == -1) {
                        fd_table_free(cfdt);
                        child->fd_table = NULL;
                        return true;
                    }
                }
            }
            lock_acquire(&filesys_lock);
            pfde = pfdt->fds[fd];
            cfde = malloc(sizeof(struct fd_entry));
            if (cfde == NULL) {
                lock_release(&filesys_lock);
                fd_table_free(cfdt);
                child->fd_table = NULL;
                return true;
            }
            cfde->type = pfde->type;
            if (cfde->type == FD_STDIN || cfde->type == FD_STDOUT) {
                cfde->file = NULL;
                cfde->ref_count = NULL;
            } else {
                cfde->file = file_duplicate(pfde->file);
                if (cfde->file == NULL) {
                    free(cfde);
                    lock_release(&filesys_lock);
                    fd_table_free(cfdt);
                    child->fd_table = NULL;
                    return true;
                }
                cfde->ref_count = malloc(sizeof *cfde->ref_count);
                if (cfde->ref_count == NULL) {
                    file_close(cfde->file);
                    free(cfde);
                    lock_release(&filesys_lock);
                    fd_table_free(cfdt);
                    child->fd_table = NULL;
                    return true;
                }
                *cfde->ref_count = 1;
            }
            lock_release(&filesys_lock);
            cfdt->fds[fd] = cfde;
        }
    }
    return false;
}

/* syscall functions */

static int
syscall_write (int fd, const void *buffer, unsigned size) {
	struct fd_table *fdt = thread_current ()->fd_table;
	if (fdt == NULL) { return -1; }
	struct fd_entry **fds = fdt->fds;
	if (fds == NULL) { return -1; }
	if(!fd_is_valid (fdt, fd)) {
		return -1;
	}
	struct fd_entry *fde = *(fds + fd);
	if (fde == NULL) { return -1; }
	if (fde->type == FD_STDOUT) {
		check_user_laddr(buffer, size);
		putbuf(buffer, size);
		return size;
	}
	struct file *opend_file = fde->file;
	if (opend_file == NULL) { return -1; }
	check_user_laddr(buffer, size);
	lock_acquire(&filesys_lock);
	int byte_written = file_write(opend_file, buffer, size);
	lock_release(&filesys_lock);

	return byte_written;
}

/* EXIT_CODE로 프로세스를 종료합니다. 이후 process_exit()이 호출됩니다. 
   비정상적인 종료시 EXIT_CODE에 -1를 지정하세요. */
static void
syscall_exit (const int exit_code) {
	thread_current()->exit_code = exit_code;
	thread_exit();
}

static int
syscall_wait(pid_t pid) {
	return process_wait (pid);
}

static pid_t
syscall_fork (const char *thread_name, struct intr_frame *f) {
	check_user_addr(thread_name);
	return process_fork(thread_name, f);
}

static int
syscall_open (const char *file) {
	if (!check_file_name (file)) {
		return -1;
	}

	lock_acquire (&filesys_lock);
	struct file *opened_file = filesys_open (file);
	lock_release (&filesys_lock);
	if (opened_file == NULL) {
		return -1;
	}

	struct fd_table *fdt = thread_current ()->fd_table;
	if (fdt == NULL) {
		file_close (opened_file);
		return -1;
	}

	int fd = fd_find_blank (fdt);
	if (fd < 0) {
		file_close (opened_file);
		return -1;
	}

	struct fd_entry *entry = malloc (sizeof *entry);
	if (entry == NULL) {
		file_close (opened_file);
		return -1;
	}

	entry->type = FD_FILE;
	entry->file = opened_file;
	entry->ref_count = malloc (sizeof *entry->ref_count);
	if (entry->ref_count == NULL) {
		free (entry);
		file_close (opened_file);
		return -1;
	}
	*entry->ref_count = 1;
	fdt->fds[fd] = entry;
	return fd;
}

static bool
syscall_remove (const char *file) {
	if (!check_file_name (file)) {
		return false;
	}

	lock_acquire (&filesys_lock);
	bool success = filesys_remove (file);
	lock_release (&filesys_lock);
	return success;
}

static void
syscall_seek (int fd, unsigned position) {
	struct file *file = get_file_from_fd (fd);
	if (file == NULL) {
		return;
	}

	lock_acquire (&filesys_lock);
	file_seek (file, position);
	lock_release (&filesys_lock);
}

static unsigned
syscall_tell (int fd) {
	struct file *file = get_file_from_fd (fd);
	if (file == NULL) {
		return 0;
	}

	lock_acquire (&filesys_lock);
	unsigned position = file_tell (file);
	lock_release (&filesys_lock);
	return position;
}

static int
syscall_dup2 (int oldfd, int newfd) {
	struct fd_table *fdt = thread_current ()->fd_table;
	struct fd_entry *old_entry; 
	struct fd_entry *new_entry;

	if (fdt == NULL || oldfd < 0 || newfd < 0 || !fd_is_valid (fdt, oldfd)) {
		return -1;
	}
	if (oldfd == newfd) {
		return newfd;
	}
	while ((size_t) newfd >= fdt->size) {
		if (fd_expaned (fdt, fdt->size << 1) < 0) {
			return -1;
		}
	}

	if (fd_is_valid (fdt, newfd)) {
		fd_entry_free (fdt, newfd);
	}

	old_entry = fd_get_entry (fdt, oldfd);
	new_entry = malloc (sizeof *new_entry);
	if (new_entry == NULL) {
		return -1;
	}
	new_entry->type = old_entry->type;
	if (old_entry->type == FD_FILE) {
		new_entry->file = old_entry->file;
		new_entry->ref_count = old_entry->ref_count;
		(*new_entry->ref_count)++;
	} else {
		new_entry->file = NULL;
		new_entry->ref_count = NULL;
	}
	fdt->fds[newfd] = new_entry;
	return newfd;
}

static int
syscall_read (int fd, const void *buffer, unsigned size) {
	struct fd_table *fdt = thread_current ()->fd_table;
	if (fdt == NULL) {
		return -1;
	}
	struct fd_entry **fds = fdt->fds;
	if (fds == NULL) {
		return -1;
	}
	if (!fd_is_valid (fdt, fd)) {
		return -1;
	}
	struct fd_entry *fde = *(fds + fd);
	if (fde == NULL) {
		return -1;
	}
	if (fde->type == FD_STDIN) {
		check_user_waddr (buffer, size);
		for (unsigned i = 0; i < size; i++) {
			*(((char *) buffer) + i) = input_getc ();
		}
		return size;
	}
	struct file *opend_file = fde->file;
	if (opend_file == NULL) {
		return -1;
	}
	check_user_waddr (buffer, size);
	lock_acquire (&filesys_lock);
	int byte_written = file_read (opend_file, buffer, size);
	lock_release (&filesys_lock);

	return byte_written;
}

static int
syscall_filesize (const int fd) {
	struct thread *cur = thread_current ();
	struct fd_table *fdt = cur->fd_table;
	struct fd_entry *fde;
	lock_acquire (&filesys_lock);
	if ((fd < 0) || (fdt == NULL)) {
		lock_release (&filesys_lock);
		return -1;
	}
	fde = fd_get_entry (fdt, fd);
	if (fde == NULL || fde->type != FD_FILE || fde->file == NULL) {
		lock_release (&filesys_lock);
		return -1;
	}
	int length = file_length (fde->file);
	lock_release (&filesys_lock);
	return length;
}

static int
syscall_exec (char *file) {
	if(!check_file_name(file)) {
		return -1;
	}
	if (process_exec (file) < 0) {
		syscall_exit(-1);
	}
	NOT_REACHED();
}

void *
syscall_mmap (void *addr, size_t length, int writable, int fd, off_t offset) {
	struct file *file = get_file_from_fd(fd);
	if(file == NULL)
		return NULL;

	// char buf[(2 << 15)] = {0};
	// off_t n_read = file_read_at(file, buf, length, offset);
	// void *pages = palloc_get_multiple (PAL_USER | PAL_ZERO, n_read / PGSIZE + 1);

	// if(pages == NULL)
	// 	return NULL;

	// memcpy(pages, buf, n_read);
	/* vm_alloc_page_with_initializer의 init은 lazy_load이고, 유효한 page가 PAGE로 넘겨진다.
	   즉, aux로 파일 오프셋 등 정보를 넘기고 init에서 초기화 할 것. */
	// vm_alloc_page_with_initializer(VM_FILE, upage, writable, init, aux);
	return do_mmap(addr, length, writable, file, offset);
}

void
sysccall_munmap (void *addr) {
	return do_munmap(addr);
}
