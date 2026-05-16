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
	list_init(&frame_table);
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
		
		pml4 = f->owner_thread->pml4;

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

		pml4 = f->owner_thread->pml4;
	
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
// ->: 공통 evict 로직 => 페이지 타입 별 작업은 file_backed_destroy에서 작업 
static struct frame *
vm_evict_frame (void) {
	struct frame *victim UNUSED = vm_get_victim ();
	/* TODO: swap out the victim and return the evicted frame. */
	/* TODO: 피해 프레임을 교체하고 제거된 프레임을 반환합니다. */
	//victim->page->operations->type;
	if(victim == NULL){
		return NULL;
	}

	// TODO: 1. victim 프레임에 들어있는 페이지의 종류를 확인
	int type = page_get_type(victim->page);
	switch (type)
	{
	// TODO: 1-1. victim이 uninit 페이지면 그냥 free
	case VM_UNINIT:
		// TODO: static 함수라 여기서 못 쓴다고 함 뭐 어떡하라고,어뚝하라고,우뜩하라고, 
		//file_backed_destroy(victim);

		// 이것도 하지 말라거?
		//pml4_clear_page(victim->owner_thread->pml4, victim->page->va);
		// victim 페이지 free
		//palloc_free_page(victim->kva);
		// -> 이거 해버리면 이제 victim이라는 값을 사용 못하나>?

		// TODO: 반환하는 victim을 재사용 가능한 빈 프레임 상태로 만들어 야 함 어 케 하 는 건 데 그 걸 
		// page -> frame, frame->page 연결을 끊어줘야 함
		// -> 프레임에 들어가있는 kva, 등을 정리해야 함 어케?? init 하면 되나 안된[ ]
		// initialize PAGE
		//uninit_new(page, pg_round_down(upage), init, type, aux, vm_page_initializer);

		// 프레임 끊기
		victim->page->frame = NULL;
		
		// 페이지 끊기
		victim->page = NULL;
		
		// thread 끊기 -> 끊기 않고 그냥 frame->page를 새로운 페이지로 할당할 때의 current_thread 를 덮어씌움 
		//victim->owner_thread = NULL;

		// frame_table에서 빼기... 이건 clock쪽에서 순회할 때 하면 될 듯 -> 프레임 자체를 재사용하는 거라 뺴면 안 됨 
		// list_
		break;
	// TODO: 1-2. victim이 anon 페이지면 swap disk에 페이지 내용 저장해두고 free
	case VM_ANON:
		// swap disk에 페이지 내용 저장
		
		// 페이지 free
		break;
	// TODO: 1-3. victim이 file 페이지면 accessed bit 확인 -> 1이면 원본 파일에 현재 파일 내용 덮어쓰기, 0이면 그냥 free 
	case VM_FILE:
		// -> vm_get_victim 안에서 해 주고 있음 
		// -> 이건 dirty bit로 해야 하네 
		// victim의 dirty bit 확인해서 1이면 원본에 덮어쓰기 0이면 그냥 프리 
		int dirty = pml4_is_dirty(victim->owner_thread->pml4, victim->page->va);

		break;
	default:
		break;
	}
	
	
	return victim;
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
	
	//void *kva = palloc_get_page(PAL_USER | PAL_ZERO);
	// palloc_get_page -> 확보한 페이지의 커널 가상 주소를 반환
	// 프레임의 kva에 넣을 값을 palloc으로 받음
	frame->kva = palloc_get_page(PAL_USER);

	// TODO: page 필드를 초기화하는 코드 필요  
	frame->page = NULL;

	// 사용 가능한 페이지 없으면?? 먼소리지 
	// 물리 페이지를 받으려고 했는데 실패한 경우 
	if(frame->kva == NULL){
		// victim 선정 -> evict 안에서 호출함
		frame = vm_evict_frame();

		// TODO: NULL이면 free할 페이지도 없음 어케 처리할지 다시 고민
		//palloc_free_page(frame->kva);

		return frame;
	}	
	else{
		// 프레임 구조체를 만듦 -> 빈 프레임 
		frame = malloc(sizeof(struct frame));
	}

	// kva가 반드시 NULL이 아닐 때만 table에 넣기 
	list_push_back(&frame_table, &frame->elem);
	frame->owner_thread = curr;
	

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
