#ifndef VM_FILE_H
#define VM_FILE_H
#include "filesys/file.h"
#include "vm/vm.h"

struct page;
enum vm_type;

struct file_page {
	struct file *reopend_file;
	off_t offset;
	size_t read_bytes;
	size_t zero_bytes;
	size_t length;
	bool writable;
};

/* file mapping info */
struct mmap_mapping_elem {
	void *addr;
	size_t page_cnt;
	struct file *reopened_file;
	struct list_elem elem;
};

void vm_file_init (void);
bool file_backed_initializer (struct page *page, enum vm_type type, void *kva);
void *do_mmap(void *addr, size_t length, int writable,
		struct file *file, off_t offset);
void do_munmap (void *va);
#endif
