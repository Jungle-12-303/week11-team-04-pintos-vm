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
	
	switch (type)
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
 * `vm_alloc_page`. */
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
		/* TODO: 페이지를 생성하고, VM 유형에 따라 초기화 함수를 불러온 다음,
         * TODO: uninit_new를 호출하여 "uninit" 페이지 구조체를 생성합니다. 
		 이후에는 uninit_new 호출 후 해당 필드를 수정해야 합니다. */

		struct page *page = malloc (sizeof (struct page));
		// initialize PAGE
		uninit_new(page, pg_round_down(upage), init, type, aux, vm_page_initializer);

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
/* 제거될 struct frame을 가져옵니다. */
static struct frame *
vm_get_victim (void) {
	struct frame *victim = NULL;
	/* TODO: The policy for eviction is up to you. */
	/* TODO: 제거 정책은 사용자가 결정합니다. */
	/* 인자 없음 -> 알아서 접근해서 처리해야 함 
	Clock 알고리즘(Second Chance)
	1. 프레임 테이블을 한 바퀴 돈다 -> 프레임 테이블에 등록된 프레임(물리 page)들을 순회 
		-> OS가 가지고 있는 현재 유저 풀 페이지들이 사용 중인 프레임 목록 => frame table 
	2. 돌면서 Accessed bit가 1인 프레임이 있으면 값을 0으로 바꿔주고 통과함
	3. 돌면서 Accessed bit가 0인 프레임이 있으면 victim으로 선정*/
	
	uint64_t *pml4;

	// thread_current()->pml4를 기준으로 모든 frame의 f->page->va를 검사
	// TODO: 페이지를 실제로 매핑한 프로세스의 pml4에서 확인 == 현재 프로세스의 pml4를 확인하는 것이 X 
	//struct thread *curr = thread_current();
	//pml4 = curr->pml4;

	if(list_empty(&frame_table))
		return NULL;
	
	struct list_elem *e, *next, *end;
	struct frame *f;

	e = list_begin(&frame_table);

	next = list_next(e);
	end = list_end(&frame_table);
	
	if(next == end && victim == NULL)
	{
		f = list_entry(e, struct frame, elem);
		
		/* TODO: 
		현재 스레드의 pml4가 아닌, accessed bit를 검사할 프레임을 소유하고 있는 스레드의 pml4를 찾아야 함 */
		struct thread *curr = thread_current();
		pml4 = curr->pml4;


		if((pml4_is_accessed(pml4, f->page->va) == 0)){
			victim = f;
			return victim;
		}
		else{
			pml4_set_accessed(pml4, f->page->va, false);
		}
	}

	while(e != end){
		f = list_entry(e, struct frame, elem);

		if(pml4_is_accessed(pml4, f->page->va) == 1){
			pml4_set_accessed(pml4, f->page->va, false);
		}			
		else{
			victim = f;
			return victim;
		}
			
		e = list_next(e);
		if(next != end)
		{
			next = list_next(e);
		}
		else{				
			e = list_begin(&frame_table);
			next = list_next(e);
		}
	}

	return victim;
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
/* 한 페이지를 제거하고 해당 프레임을 반환합니다.
 * 오류 발생 시 NULL을 반환합니다.*/
static struct frame *
vm_evict_frame (void) {
	struct frame *victim UNUSED = vm_get_victim ();
	/* TODO: swap out the victim and return the evicted frame. */
	/* TODO: 피해 프레임을 교체하고 제거된 프레임을 반환합니다. */
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
	struct frame *victim = NULL;
	struct thread *curr = thread_current();
	
	frame = malloc(sizeof(struct frame));
	
	// TODO: page 필드를 초기화하는 코드 필요 
	

	//void *kva = palloc_get_page(PAL_USER | PAL_ZERO);
	// palloc_get_page -> 확보한 페이지의 커널 가상 주소를 반환
	frame->kva = palloc_get_page(PAL_USER);
	frame->page = NULL;

	// 사용 가능한 페이지 없으면
	if(frame->kva == NULL){
		// victim 선정
		victim = vm_get_victim();

		// eviction
		frame = vm_evict_frame();

		// TODO: NULL이면 free할 페이지도 없음 어케 처리할지 다시 고민
		//palloc_free_page(frame->kva);
	}
	else{	
		// kva가 반드시 NULL이 아닐 때만 table에 넣기 
		list_push_back(&frame_table, &frame->elem);
		frame->owner_thread = curr;
	}

	ASSERT (frame != NULL);
	//frame->kva = kva;
	//ASSERT (frame->page == NULL);

	return frame;
}

/* Growing the stack. */
static void
vm_stack_growth (void *addr UNUSED) {
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
	/* TODO: Validate the fault */
	/* TODO: Your code goes here */
	// TODO: stack 밑인가? kernel? user?
	// 밑이면 grow 아니면 프로세스 종료
	struct page *found = spt_find_page(&spt->hash, addr);
	if(found == NULL) return false;

	return vm_do_claim_page (found);
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
	page = spt_find_page(&thread_current()->spt, va);
	if(page == NULL) return false;
	return vm_do_claim_page (page);
}

/* Claim the PAGE and set up the mmu. */
static bool
vm_do_claim_page (struct page *page) {
	struct frame *frame = vm_get_frame ();

	/* Set links */
	frame->page = page;
	page->frame = frame;

	/* TODO: Insert page table entry to map page's VA to frame's PA. */
	if(!pml4_set_page(thread_current()->pml4, page->va, frame->kva, true)) {
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
