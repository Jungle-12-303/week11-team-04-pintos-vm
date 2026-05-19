/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"
#include "threads/vaddr.h"
#include "threads/mmu.h"
#include "threads/thread.h"
#include <string.h>

struct list frame_table;

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
/* 페이지의 유형을 가져옵니다.
 * 이 함수는 페이지가 초기화된 후 그 유형을 확인하고자 할 때 유용합니다.
 * 이 함수는 현재 완전히 구현되었습니다. */
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
		/* TODO: 페이지를 생성하고, VM 유형에 따라 초기화 함수를 불러온 다음,
         * TODO: uninit_new를 호출하여 "uninit" 페이지 구조체를 생성합니다. 
		 이후에는 uninit_new 호출 후 해당 필드를 수정해야 합니다. */

		struct page *page = malloc (sizeof (struct page));

		// initialize PAGE
		//uninit_new(page, pg_round_down(upage), init, type, aux, vm_page_initializer);

		// VM_TYPE(type): 보조 정보를 제외한 타입 정보만 얻을 수 있는 매크로
		switch (VM_TYPE(type))
		{
		// 이 분기는 uninit 타입 페이지에서 다른 페이지로 전환하기 위한 처리를 하는 분기이기 때문에 애초에 VM_UNINIT일 때의 처리를 해 주지 않아도 됨
		// case VM_UNINIT:
		// 	uninit_new(page, pg_round_down(upage), init, type, aux, uninit_initialize);
		// 	//ASSERT (type != VM_UNINIT)
		// 	// free(kpage);
		// 	//return false;
		// 	break;
		case VM_ANON:
			uninit_new(page, pg_round_down(upage), init, type, aux, anon_initializer);
			break;
		case VM_FILE:
			uninit_new(page, pg_round_down(upage), init, type, aux, file_backed_initializer);
			break;
		// default:
		// 	// free(kpage);
		// 	return false;
		// 	break;
		}

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
			
		//e = next;
		// next가 end가 아닐 때 = next가 마지막 요소가 될 때까지 해당 
		if(next != end)
		{
			// 여기서 e가 마지막 요소가 될 수 있음
			e = next;
			next = list_next(e);
		}
		// next가 end일 때 = e가 마지막 요소일 떄 
		else{
			e = list_begin(&frame_table);
			next = list_next(e);
		}
	}

	ASSERT(victim != NULL);

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

	//ASSERT(is_kernel_vaddr(victim->kva));
	
	// 함수 포인터로 각 페이지 타입에 맞는 swap_out 함수 실행 
	bool swapped = swap_out(victim->page);
	
	ASSERT(is_kernel_vaddr(victim->kva));
	ASSERT(swapped);
	
	// 현재 오너 스레드의 pml4와 PA 매핑을 끊어줌  
	pml4_clear_page(victim->owner_thread->pml4, victim->page->va);
	
	// 프레임 끊기	
	victim->page->frame = NULL;
	
	// 페이지 끊기
	victim->page = NULL;
	
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
	
	// 프레임 구조체를 만듦 -> 빈 프레임 
	frame = malloc(sizeof(struct frame));

	ASSERT(frame);

	// 프레임의 kva에 넣을 페이지를 palloc으로 받음
	// TODO: palloc으로 할당된 페이지는 쓰레기 값 남아있을 수 있어서 초기화 필요
	frame->kva = palloc_get_page(PAL_USER|PAL_ZERO);
	//memset(frame->kva, 0, PGSIZE);

	// 사용 가능한 페이지 없으면
	// = 물리 페이지를 받으려고 했는데 실패한 경우 
	if(frame->kva == NULL){
		// vm_evict_frame으로 반환되는 frame은 이미 malloc 되어있는 프레임이라
		// 위에서 malloc한 frame을 free 해줘야 누수 안 생김 
		free(frame);
		
		// victim 선정 -> evict 안에서 호출함
		frame = vm_evict_frame();

		//ASSERT(frame);
		ASSERT(is_kernel_vaddr(frame->kva));
	}
	else{	
		// kva가 반드시 NULL이 아닐 때만 table에 넣기 
		// evict 된 프레임은 원래 프레임 테이블에 있는 상태 그대로라 테이블에 다시 넣어주면 안 됨
		list_push_back(&frame_table, &frame->elem);
		frame->owner_thread = curr;
	}

	// TODO: 예외 처리 명세 충족 X
	// 회수 로직 추가 필요 
	ASSERT (frame != NULL);

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
		return vm_do_claim_page(found);
	}
	uint64_t *upgae = pg_round_down(addr);
	if((uint64_t *)addr >=f->rsp - 8 && USER_STACK > (uint64_t *)addr && (uint64_t *)addr >= USER_STACK - (PGSIZE << 8)) {
		return vm_stack_growth(upgae);
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
	struct thread *curr = thread_current();
	/* Set links */
	frame->page = page;
	page->frame = frame;
	frame->owner_thread = curr;
	// TODO: frame->kva 넘겨주기 


	/* TODO: Insert page table entry to map page's VA to frame's PA. */
	/* TODO: 페이지의 가상 주소(VA)를 프레임의 물리 주소(PA)에 매핑하는 페이지 테이블 항목을 삽입합니다. */
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
