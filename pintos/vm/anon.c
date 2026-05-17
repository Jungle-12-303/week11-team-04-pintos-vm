/* anon.c: Implementation of page for non-disk image (a.k.a. anonymous page). */

#include "vm/vm.h"
#include "devices/disk.h"
#include "lib/kernel/bitmap.h"
#include "threads/synch.h"

/* DO NOT MODIFY BELOW LINE */
// swap 용도 disk 할당 구간
static struct disk *swap_disk;
// 비트맵 으로 표현된 swap 목록, 1,5,8이면, 10010001 0000000 과 같이 표현
static struct bitmap *swap_table;
// 동시성 오류 방지를 위한 락
static struct lock swap_lock;
// 마지막으로 검색한 인덱스 번호
static size_t swap_last_index = 0;
static bool anon_swap_in (struct page *page, void *kva);
static bool anon_swap_out (struct page *page);
static void anon_destroy (struct page *page);
static void swap_release(struct anon_page *anon_page);

/* DO NOT MODIFY this struct */
static const struct page_operations anon_ops = {
	.swap_in = anon_swap_in,
	.swap_out = anon_swap_out,
	.destroy = anon_destroy,
	.type = VM_ANON,
};

/* Initialize the data for anonymous pages */
void
vm_anon_init (void) {
	/* TODO: Set up the swap_disk. */
	// swap_disk로 사용할 disk 지정
	swap_disk = disk_get(1,1);
	ASSERT(swap_disk);
	// 비트맵으로 검사할 전역변수 테이블 사용
	swap_table = bitmap_create(disk_size(swap_disk) / 8);
	ASSERT(swap_table);
	// 동시에 table 작업하면 오류 발생 가능성이 존재하여 전역변수로 lock 생성
	lock_init(&swap_lock);
}

/* Initialize the file mapping */
bool
anon_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	page->operations = &anon_ops;
	ASSERT(VM_TYPE(type) == VM_ANON);
	struct anon_page *anon_page = &page->anon;
	// swap_table 할당 전 slot 초기화 필요, size_t에 -1이 없어서 최대값으로 할당
	anon_page->swap_slot = SWAP_SLOT_INVALID;
	return true;
}

/* Swap in the page by read contents from the swap disk. */
static bool
anon_swap_in (struct page *page, void *kva) {
	// 디스크 -> kva로 전달
	struct anon_page *anon_page = &page->anon;
	size_t swap_slot = anon_page->swap_slot;
	// lock 걸어서 동시성 오류 방지
	lock_acquire(&swap_lock);

	// swap_slot 유효성 검사
	if (swap_slot == SWAP_SLOT_INVALID) {
		lock_release(&swap_lock);
		return true;
	}
	if ((swap_slot >= (disk_size(swap_disk) / 8))) {
		lock_release(&swap_lock);
		return false;
	}
	if ((!bitmap_test(swap_table,swap_slot))) {
		lock_release(&swap_lock);
		return false;
	}

	// 디스크를 읽어서 kva에 전달, sector의 크기가 512byte 고정된 크기라서 8번 거쳐 전달
	for(int i=0;i<8;i++) {
		disk_read(swap_disk,swap_slot * 8 + i,((void *)((char *) kva) + i * DISK_SECTOR_SIZE));
	}
	// 스왑 초기화 및 락 풀림
	swap_release(anon_page);
	lock_release(&swap_lock);
	return true;
}

/* Swap out the page by writing contents to the swap disk. */
static bool
anon_swap_out (struct page *page) {
	struct anon_page *anon_page = &page->anon;
	ASSERT(page->frame != NULL);
	ASSERT((page->frame)->kva != NULL);
	lock_acquire(&swap_lock);
	if (anon_page->swap_slot != SWAP_SLOT_INVALID) {
		lock_release(&swap_lock);
		return false;
	}
	// 마지막 지점부터 찾기
	size_t start_index = (swap_last_index >= (disk_size(swap_disk) / 8)) ? 0 : swap_last_index;
	anon_page->swap_slot = bitmap_scan(swap_table,start_index,1,0);
	if (anon_page->swap_slot == BITMAP_ERROR) {
		// 다시 찾기, 해당 부분때문에 일반적으로 앞에서 1번만 찾는 로직이 2번 찾는 문제가 발생 가능
		start_index = 0;
		anon_page->swap_slot = bitmap_scan(swap_table,start_index,1,0);
	}
	if (anon_page->swap_slot == BITMAP_ERROR) {
		/* 처음부터 다시 찾았는데도 없으면 오류 리턴
		스왑 디스크가 모두 차서 아무것도 찾지 못할 때 false 리턴
		필요하면 추가 구현 필요
		*/
		anon_page->swap_slot = SWAP_SLOT_INVALID;
		lock_release(&swap_lock);
		return false;
	}
	swap_last_index = anon_page->swap_slot + 1;
	bitmap_set(swap_table,anon_page->swap_slot,1);
	for(int i=0;i<8;i++) {
		disk_write(swap_disk,anon_page->swap_slot * 8 + i,(void *)((char *)((page->frame)->kva) + i * DISK_SECTOR_SIZE));
	}
	lock_release(&swap_lock);
	return true;
}

/* Destroy the anonymous page. PAGE will be freed by the caller. */
static void
anon_destroy (struct page *page) {
	// 해당하는 anon 페이지를 초기화하는 함수
	ASSERT(swap_table != NULL);
	struct anon_page *anon_page = &page->anon;
	lock_acquire(&swap_lock);
	swap_release(anon_page);
	lock_release(&swap_lock);
	
}

static void swap_release(struct anon_page *anon_page) {
	// 해당하는 anon 페이지를 초기화하는 함수
	if (
		(anon_page->swap_slot != SWAP_SLOT_INVALID) && 
		(anon_page->swap_slot < (disk_size(swap_disk) / 8)) && 
		(bitmap_test(swap_table,anon_page->swap_slot))
	) {
		bitmap_reset(swap_table,anon_page->swap_slot);
		anon_page->swap_slot=SWAP_SLOT_INVALID;
	} else if (
		(anon_page->swap_slot != SWAP_SLOT_INVALID) && 
		(anon_page->swap_slot < (disk_size(swap_disk) / 8)) && 
		(!bitmap_test(swap_table,anon_page->swap_slot))
	) {
		anon_page->swap_slot=SWAP_SLOT_INVALID;
	}
}
