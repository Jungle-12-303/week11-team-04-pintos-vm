/* file.c: Implementation of memory backed file object (mmaped object). */

#include "vm/vm.h"
#include "threads/vaddr.h"
#include "threads/malloc.h"
#include "threads/mmu.h"
#include "lib/round.h"
#include <string.h>

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
}

/* Swap in the page by read contents from the file. */
static bool
file_backed_swap_in (struct page *page, void *kva) {
	struct file_page *file_page UNUSED = &page->file;
}

/* Swap out the page by writeback contents to the file. */
static bool
file_backed_swap_out (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;
}

/* Destory the file backed page. PAGE will be freed by the caller. */
/* 파일 기반 페이지를 삭제합니다. PAGE는 호출 측에서 해제합니다. */
static void
file_backed_destroy (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;
}

struct mmap_mapping_elem *
mmap_mapping_find (void *addr) {
	struct thread *curr = thread_current ();
	struct list *l = &curr->mmap_mapping;
	struct list_elem *e = list_begin(l);
	struct mmap_mapping_elem *entry = NULL;

	for (; e != list_end(l); e = list_next(e)) {
		entry = list_entry(e, struct mmap_mapping_elem, elem);
		if(entry->addr == addr) return entry;
	}
	return NULL;
}

static bool
lazy_load_file (struct page *page, void *aux) {
	/* vm_page_initializer에서 type이 바뀌었다는 가정이 있음 */
	// ASSERT (page->operations->type == VM_FILE);
	struct file_page *faux = aux;
	/* page->file에 aux metadata 저장 */
	page->file = *faux;

	void *kva = page->frame->kva;
	off_t actual_read_bytes = file_read_at(faux->reopend_file, 
		kva, faux->read_bytes, 
		faux->offset);
	memset((uint8_t *) kva + actual_read_bytes, 0, PGSIZE-actual_read_bytes);
	free(faux);
	return true;
}

/* Do the mmap */
void *
do_mmap (void *addr, size_t length, int writable,
		struct file *file, off_t offset) {
	if ((uint64_t)addr != pg_round_down (addr) || length == 0)
		return NULL;
	if (spt_find_page (&thread_current ()->spt, addr) != NULL)
		return NULL;
	struct file *reopen_file = file_reopen (file);
	size_t page_cnt = DIV_ROUND_UP (length, PGSIZE);
	size_t remain_bytes = length;

	/* initializing */
	for (size_t i = 0; i < page_cnt; i++) {
		size_t page_offset = offset + i * PGSIZE;
		size_t read_bytes = remain_bytes < PGSIZE ? remain_bytes : PGSIZE;
		size_t zero_bytes = PGSIZE - read_bytes;
		remain_bytes -= read_bytes;
		void *upage = (uint8_t *) addr + PGSIZE * i;

		/* aux에 file metadata 저장 */
		struct file_page *aux = malloc (sizeof (struct file_page));
		aux->reopend_file = reopen_file;
		aux->offset = page_offset;
		aux->read_bytes = read_bytes;
		aux->zero_bytes = zero_bytes;
		
		if (!vm_alloc_page_with_initializer (VM_FILE, upage, writable, lazy_load_file, aux)) {
			return false;
		}
	}

	/* insert into mmap mapping */
	struct list *mmap_mapping = &thread_current ()->mmap_mapping;
	struct mmap_mapping_elem *e = malloc (sizeof (struct mmap_mapping_elem));
	e->addr = addr;
	e->page_cnt = page_cnt;
	e->reopened_file = reopen_file;
	if (e == NULL)
		return NULL;
	enum intr_level old_level = intr_disable ();
	list_push_back (mmap_mapping, &e->elem);
	intr_set_level (old_level);
	return addr;
}

/* Do the munmap */
void
do_munmap (void *addr) {
	struct page *page = NULL;
	void *upage = addr;

	struct thread *curr = thread_current();
	struct supplemental_page_table *spt = &curr->spt;
	uint64_t *pml4 = curr->pml4;
	struct mmap_mapping_elem *e = mmap_mapping_find(addr);
	if(e == NULL) {
		printf("error (do_munmap): element is NULL. %d\n", list_size(&curr->mmap_mapping));
		return;
	}
	for(size_t page_cnt = 0; 
		page_cnt < e->page_cnt; 
		page_cnt++, upage = (uint8_t *) upage + PGSIZE) {
		page = spt_find_page(spt, upage);
		bool loaded = page->frame!= NULL;
		bool dirty = pml4_is_dirty (pml4, upage);
		if (e->reopened_file == NULL) 
			page->file.reopend_file;
		if (loaded && dirty) {
			file_write_at(page->file.reopend_file, 
				page->frame->kva,
				page->file.read_bytes, 
				page->file.offset);
		}
		spt_remove_page (spt, page);
	}

	file_close (e->reopened_file);
	enum intr_level old_level = intr_disable();
	list_remove(&e->elem);

	intr_set_level(old_level);
}
