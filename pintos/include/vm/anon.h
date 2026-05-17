#ifndef VM_ANON_H
#define VM_ANON_H
#include "vm/vm.h"
struct page;
enum vm_type;

struct anon_page {
  size_t swap_slot;
};

#define SWAP_SLOT_INVALID ((size_t) - 1)

void vm_anon_init (void);
bool anon_initializer (struct page *page, enum vm_type type, void *kva);

#endif


// struct page {
// 	const struct page_operations *operations;
// 	void *va;              /* Address in terms of user space */
// 	struct frame *frame;   /* Back reference for frame */

// 	/* Your implementation */

// 	/* Per-type data are binded into the union.
// 	 * Each function automatically detects the current union */
// 	union {
// 		struct uninit_page uninit;
// 		struct anon_page anon;
// 		struct file_page file;
// #ifdef EFILESYS
// 		struct page_cache page_cache;
// #endif
// 	};
// };