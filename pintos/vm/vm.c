/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"
#include "threads/vaddr.h"
#include "threads/mmu.h"
#include "threads/thread.h"
#include <string.h>

/* Initializes the virtual memory subsystem by invoking each subsystem's
 * intialize codes. */
void
vm_init (void) {
	vm_anon_init ();
	vm_file_init ();
#ifdef EFILESYS  /* For project 4 */
	pagecache_init ();
#endif
	register_inspect_intr ();
	/* DO NOT MODIFY UPPER LINES. */
	/* TODO: Your code goes here. */
}

/* Get the type of the page. This function is useful if you want to know the
 * type of the page after it will be initialized.
 * This function is fully implemented now. */
enum vm_type
page_get_type (struct page *page) {
	int ty = VM_TYPE (page->operations->type);
	switch (ty) {
		case VM_UNINIT:
			return VM_TYPE (page->uninit.type);
		default:
			return ty;
	}
}

/* Helpers */
static struct frame *vm_get_victim (void);
static bool vm_do_claim_page (struct page *page);
static struct frame *vm_evict_frame (void);

/**
 * @brief vm_page_intializer for `vm.h`
 * 실제 물리 메모리를 할당 받아서 PAGE에 직접 매핑해준다.
 * TODO: pml4 등록, vm_claim과의 차이
 * @author yoonki1207
 */
bool
vm_page_initializer (struct page *page, enum vm_type type, void *kva) {
	// TODO: Your code here
	/*
	initialize va, frmae, change into anon or file
	for user? kernel?
	*/
	ASSERT (page != NULL);
	struct supplemental_page_table *spt = &thread_current()->spt;
	
	switch (page_get_type(page))
	{
	case VM_UNINIT:
		ASSERT (type != VM_UNINIT)
		// free(kpage);
		return false;
		break;
	case VM_ANON:
		// anon_initializer(page, type, kva);
		// memset(kpage, 0, PGSIZE); // FIXME: anon_initializer
		break;
	case VM_FILE:
		// memset(kpage, 0, PGSIZE); // FIXME: do not init in 0s
		break;
	
	default:
		// free(kpage);
		return false;
		break;
	}
	// page->va = ptov(kva); // TODO: make it valid in `thread/vaddr.h`
	if(page->frame == NULL) {
		page->frame = malloc (sizeof (struct frame));
		if(page->frame == NULL) {
			// free(kpage);
			return false;
		}
		page->frame->kva = kva;
		page->frame->page = page;
	}
	return true;
}

/* Create the pending page object with initializer. If you want to create a
 * page, do not create it directly and make it through this function or
 * `vm_alloc_page`. 
 * UPGAE는 pg_roudn_down되어야 합니다.
 * */
bool
vm_alloc_page_with_initializer (enum vm_type type, void *upage, bool writable,
		vm_initializer *init, void *aux) {

	ASSERT (VM_TYPE(type) != VM_UNINIT)

	struct supplemental_page_table *spt = &thread_current ()->spt;

	/* Check wheter the upage is already occupied or not. */
	if (spt_find_page (spt, upage) == NULL) {
		/* TODO: Create the page, fetch the initialier according to the VM type,
		 * TODO: and then create "uninit" page struct by calling uninit_new. You
		 * TODO: should modify the field after calling the uninit_new. */
		struct page *page = malloc (sizeof (struct page));
		// initialize PAGE
		uninit_new(page, pg_round_down(upage), init, type, aux, vm_page_initializer);
		page->writable = writable;

		/* TODO: Insert the page into the spt. */
		if(!spt_insert_page(spt, page)) {
			return false;
		} else {
			return true;
		}
	}
err:
	return false;
}

/* Find VA from spt and return page. On error, return NULL. */
struct page *
spt_find_page (struct supplemental_page_table *spt UNUSED, void *va UNUSED) {
	struct page page;
	/* TODO: Fill this function. */
	page.va = pg_round_down(va);
	struct hash_elem *e = hash_find(&spt->hash, &page.hash_elem);

	return e == NULL ? NULL : hash_entry (e, struct page, hash_elem);
}

/* Insert PAGE into spt with validation. */
bool
spt_insert_page (struct supplemental_page_table *spt UNUSED,
		struct page *page UNUSED) {
	int succ = false;
	hash_insert(&spt->hash, &page->hash_elem);
	succ = true; // TODO: hash_insert 확인
	return succ;
}

void
spt_remove_page (struct supplemental_page_table *spt, struct page *page) {
	vm_dealloc_page (page);
	return true;
}

/* Get the struct frame, that will be evicted. */
static struct frame *
vm_get_victim (void) {
	struct frame *victim = NULL;
	 /* TODO: The policy for eviction is up to you. */

	return victim;
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
static struct frame *
vm_evict_frame (void) {
	struct frame *victim UNUSED = vm_get_victim ();
	/* TODO: swap out the victim and return the evicted frame. */

	return NULL;
}

/* palloc() and get frame. If there is no available page, evict the page
 * and return it. This always return valid address. That is, if the user pool
 * memory is full, this function evicts the frame to get the available memory
 * space.*/
static struct frame *
vm_get_frame (void) {
	struct frame *frame = NULL;
	/* TODO: Fill this function. */
	frame = malloc(sizeof(struct frame));
	void *kva = palloc_get_page(PAL_USER | PAL_ZERO);
	ASSERT (frame != NULL);
	// ASSERT (frame->page == NULL);
	frame->kva = kva;
	return frame;
}

/* Growing the stack. */
static bool
vm_stack_growth (void *addr UNUSED) {
	return vm_alloc_page(VM_ANON | VM_MARKER_0, pg_round_down(addr), true) && vm_claim_page(pg_round_down(addr));
}

/* Handle the fault on write_protected page */
static bool
vm_handle_wp (struct page *page UNUSED) {
}

/* Return true on success */
bool
vm_try_handle_fault (struct intr_frame *f UNUSED, void *addr UNUSED,
		bool user UNUSED, bool write UNUSED, bool not_present UNUSED) {
	struct supplemental_page_table *spt UNUSED = &thread_current ()->spt;
	struct page *page = NULL;
	/* 권한 체크 */
	if(addr == NULL || is_kernel_vaddr(addr)) {
		return false;
	}
	if(!not_present) {
		return false;
	}
	struct page *found = spt_find_page(spt, addr);
	if(found != NULL) {
		if(write && !found->writable)
			return false;
		return vm_do_claim_page(found);
	}
	uint64_t *upgae = pg_round_down(addr);
	if (user) {
		if ((uint64_t *)addr >= f->rsp - 8 && USER_STACK > (uint64_t *)addr && (uint64_t *)addr >= USER_STACK - (PGSIZE << 8)) {
			return vm_stack_growth(upgae);
		} 
	} else {
		if ((uint64_t *)addr >= thread_current ()->user_rsp - 8 && USER_STACK > (uint64_t *)addr && (uint64_t *)addr >= USER_STACK - (PGSIZE << 8)) {
			return vm_stack_growth(upgae);
		} 
	}
	return false;
}

/* Free the page.
 * DO NOT MODIFY THIS FUNCTION. */
void
vm_dealloc_page (struct page *page) {
	destroy (page);
	free (page);
}

/* Claim the page that allocate on VA. */
bool
vm_claim_page (void *va UNUSED) {
	struct page *page = NULL;
	/* TODO: Fill this function */
	struct supplemental_page_table *spt = &thread_current()->spt;
	if (spt == NULL)
		return false;
	page = spt_find_page(spt, pg_round_down(va));
	if(page == NULL) return false;
	return vm_do_claim_page (page);
}

/* Claim the PAGE and set up the mmu. */
static bool
vm_do_claim_page (struct page *page) {

	if(page == NULL) {
		return false;
	}
	struct frame *frame = vm_get_frame ();

	/* Set links */
	frame->page = page;
	page->frame = frame;

	/* TODO: Insert page table entry to map page's VA to frame's PA. */
	if(!pml4_set_page(thread_current()->pml4, page->va, frame->kva, page->writable)) {
		printf("vm_do_claim_page(): pml4_set_page failed\n");
		return false;
	}

	return swap_in (page, frame->kva);
}

/* hash less function */
static bool
page_less (const struct hash_elem *a_,
           const struct hash_elem *b_, void *aux UNUSED) {
  const struct page *a = hash_entry (a_, struct page, hash_elem);
  const struct page *b = hash_entry (b_, struct page, hash_elem);

  return a->va < b->va;
}


unsigned
page_hash (const struct hash_elem *p_, void *aux UNUSED) {
  const struct page *p = hash_entry (p_, struct page, hash_elem);
  return hash_bytes (&p->va, sizeof p->va);
}


/* Initialize new supplemental page table */
void
supplemental_page_table_init (struct supplemental_page_table *spt UNUSED) {
	hash_init(&spt->hash, page_hash, page_less, NULL);
}

/* Copy supplemental page table from src to dst */
bool
supplemental_page_table_copy (struct supplemental_page_table *dst UNUSED,
		struct supplemental_page_table *src UNUSED) {
}

/* Free the resource hold by the supplemental page table */
void
supplemental_page_table_kill (struct supplemental_page_table *spt UNUSED) {
	/* TODO: Destroy all the supplemental_page_table hold by thread and
	 * TODO: writeback all the modified contents to the storage. */
}
