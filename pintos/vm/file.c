/* file.c: Implementation of memory backed file object (mmaped object). */

#include "vm/vm.h"
#include "userprog/process.h"
#include "userprog/syscall.h"

static bool file_backed_swap_in (struct page *page, void *kva);
static bool file_backed_swap_out (struct page *page);
static void file_backed_destroy (struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations file_ops = {
	.swap_in = file_backed_swap_in,
	.swap_out = file_backed_swap_out,
	.destroy = file_backed_destroy,
	.type = VM_FILE,
};

/* The initializer of file vm */
void
vm_file_init (void) {
}

/* Initialize the file backed page */
bool
file_backed_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	page->operations = &file_ops;

	struct file_page *file_page = &page->file;

	// 파일 정보 넣기
	struct segment *seg =(struct segment *) &(page->aux);
	file_page->file = seg->file;
	file_page->writable = seg->writable;
	file_page->ofs = seg->ofs;
	file_page->length = file_length(seg->file);
	return true;
}

/* Swap in the page by read contents from the file. */
static bool
file_backed_swap_in (struct page *page, void *kva) {
	// Swaps in a page at kva by reading the contents in from the file. You need to synchronize with the file system.
	struct file_page *file_page UNUSED = &page->file;
	if (file_page->file == NULL) {
		return false;
	}
	// file-backed page 하나는 파일 전체가 아니라 보통 한 page 구간만 담당합니다. 마지막 page는 read_bytes가 PGSIZE보다 작을 수도 있고, 나머지는 zero-fill이어야 합니다. 지금처럼 파일 전체 길이를 kva에 읽으면 frame 한 장 범위를 넘길 가능성이 큽니다.
	int is_success = file_read_at_lock(file_page->file, kva, file_page->length,file_page->ofs);
	if (is_success == -1) {
		return false;
	}
	return true;
}

/* Swap out the page by writeback contents to the file. */
static bool
file_backed_swap_out (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;
	// MEMORY -> DISK
	ASSERT(page->frame != NULL);
	ASSERT((page->frame)->kva != NULL);
	// file-backed page 하나는 파일 전체가 아니라 보통 한 page 구간만 담당합니다. 마지막 page는 read_bytes가 PGSIZE보다 작을 수도 있고, 나머지는 zero-fill이어야 합니다. 지금처럼 파일 전체 길이를 kva에 읽으면 frame 한 장 범위를 넘길 가능성이 큽니다.
	// zero fill
	// writable 여부 확인, dirty 확인
	file_write_at_lock(file_page->file,(page->frame)->kva,file_page->length,file_page->ofs);
	(page->frame)->kva = NULL;
	page->frame = NULL;
	return true;
}

/* Destory the file backed page. PAGE will be freed by the caller. */
static void
file_backed_destroy (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;
}

/* Do the mmap */
void *
do_mmap (void *addr, size_t length, int writable,
		struct file *file, off_t offset) {
}

/* Do the munmap */
void
do_munmap (void *addr) {
}
