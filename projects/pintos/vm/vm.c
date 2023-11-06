/* vm.c: Generic interface for virtual memory objects. */

/*
	User Pool
	: 사용자 프로세스에서 사용할 수 있는 물리 메모리 frame의 pool

	User Page
	: 사용자 프로세스의 가상 주소 공간에 있는 특정 가상 페이지

	User Space
	: 사용자 프로세스의 전체 가상 주소 공간, User Page + Kernel Page
*/

#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"

#include "threads/vaddr.h"

struct list frame_table;
struct list_elem* start;

extern struct lock frame_lock;

/*
	Project 3: Frame 테이블 전역 변수 선언
	vm.c 파일의 모든 함수가 해당 frame_table을 공유
*/
struct list frame_table;
struct list_elem *start;

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

	// Project 3: Init frame table
	list_init(&frame_table);
	start = list_begin(&frame_table);

	lock_init(&frame_lock);
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

/* Create the pending page object with initializer. If you want to create a
 * page, do not create it directly and make it through this function or
 * `vm_alloc_page`. */
/*
	vm_alloc_page_with_initializer: 타입에 따라 적절한 이니셜라이져를 가져와 uninit_new를 호출하는 함수
	항상 uninit type의 페이지를 생성함, 그 후에 uninit_new을 통해 받아온 type으로 페이지를 구성함

	즉 이 함수는 커널이 새로운 페이지를 요청할 때 호출됨
	페이지는 하나의 라이프사이클을 가짐: Initialize -> Page fault -> Lazy load -> Swap in -> Swap out -> Destroy
*/
bool
vm_alloc_page_with_initializer (enum vm_type type, void *upage, bool writable, vm_initializer *init, void *aux) {
	ASSERT (VM_TYPE(type) != VM_UNINIT)

	struct supplemental_page_table *spt = &thread_current ()->spt;

	/* Check wheter the upage is already occupied or not. */
	if (spt_find_page (spt, upage) == NULL) {
		/* TODO: Create the page, fetch the initialier according to the VM type,
		 * TODO: and then create "uninit" page struct by calling uninit_new. You
		 * TODO: should modify the field after calling the uninit_new. */

		struct page* page = (struct page*)malloc(sizeof(struct page));
		if (page == NULL) {
			goto err;
		}

        typedef bool (*initializerFunc)(struct page *, enum vm_type, void *);
        initializerFunc initializer = NULL;

        switch(VM_TYPE(type)) {
            case VM_ANON:
                initializer = anon_initializer;
                break;
            case VM_FILE:
                initializer = file_backed_initializer;
                break;
		}

        uninit_new(page, upage, init, type, aux, initializer);

        page->writable = writable;

		/* TODO: Insert the page into the spt. */
		return spt_insert_page(spt, page);
	}
err:
	return false;
}

/* Find VA from spt and return page. On error, return NULL. */
/*
	spt_find_page: spt에서 va가 있는지를 찾는 함수, hash_find() 사용

	pg_round_down: 해당 va가 속해 있는 page의 시작 주소를 얻는 함수
	hash_find: Dummy page의 빈 hash_elem을 넣어주면, va에 맞는 hash_elem을 리턴해주는 함수 (hash_elem 갱신)

	hash_find가 NULL을 리턴할 수 있으므로, 리턴 시 NULL Check
*/
struct page *
spt_find_page (struct supplemental_page_table *spt UNUSED, void *va UNUSED) {
	struct page *page = (struct page *)malloc(sizeof(struct page));
	page->va = pg_round_down(va);

	struct hash_elem *e = hash_find(&spt->spt_hash, &page->hash_elem);

	free(page);

	return e == NULL ? NULL : hash_entry(e, struct page, hash_elem);
}

/* Insert PAGE into spt with validation. */
bool
spt_insert_page (struct supplemental_page_table *spt UNUSED, struct page *page UNUSED) {
	return page_insert(&spt->spt_hash, page);
}	

void
spt_remove_page (struct supplemental_page_table *spt, struct page *page) {
	vm_dealloc_page (page);
	return true;
}

/* Get the struct frame, that will be evicted. */
/*
	vm_get_victim: Frame Table에서 해당 정보를 지우는 함수

	Accessed Bit: 해당 페이지에 대한 접근이 있었는지 기록, 페이지 교체 알고리즘에 사용됨
	accessed bit을 0으로 설정하기 위해 pml4_set_accessed()에서 세번째 인자 0으로 넣어줌
*/
static struct frame *
vm_get_victim (void) {
	// 1) 현재 스레드의 pml4를 가져옴
	struct thread *curr = thread_current();
	uint64_t pml4 = &curr->pml4;

	// 2) Frame 테이블을 순회하며 victim을 찾고, accessed로 변경
	struct frame *victim = NULL;
	
	struct list_elem *e = start;
	
	/*
		가장 오래 전에 참조된 페이지를 알기에는 어려움이 있음, 대신 최근에 사용되지 않은 페이지를 쫓아내는 것

		Clock Algorithm, Accessed bit를 사용하여 교체 대상 페이지 선정
		1) Accessed bit가 0인 페이지를 찾을 때까지 하나씩 이동
		2) 이동하는 중 1인 것은 모두 0으로 바꿈
		3) 이동하는 중 0인 것을 찾으면 해당 페이지가 victim이 됨
	*/

	/*
		1) pml4_is_accessed() 리턴값이 true인 경우, 즉 PTE가 최근에 액세스된 경우
		pml4_set_accessed()를 이용하여 accessed bit를 0으로 설정

		2) pml4_is_accessed() 리턴값이 false인 경우, 즉 pml4에 PTE가 없는 경우
		바로 그 frame이 victim이 됨

		3) 필요한 경우 맨 처음부터 한번 더 돌려 victim을 찾아냄
	*/

	for (start = e; start != list_end(&frame_table); start = list_next(start)) {
		victim = list_entry(start, struct frame, frame_elem);
		
		if (pml4_is_accessed(curr->pml4, victim->page->va)) {
			pml4_set_accessed(curr->pml4, victim->page->va, 0);

		} else {
			return victim;
		}
	}

	for (start = list_begin(&frame_table); start != e; start = list_next(start)) {
		victim = list_entry(start, struct frame, frame_elem);
		
		if (pml4_is_accessed(curr->pml4, victim->page->va)) {
			pml4_set_accessed(curr->pml4, victim->page->va, 0);

		} else {
			return victim;
		}
	}
	
	return victim;
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
/*
	vm_evict_frame: User pool에 사용 가능한 page가 없을 경우 Swap out을 진행하는 함수

	frame을 evict하는 함수이므로, NULL을 반환해주면 됨
*/
static struct frame *
vm_evict_frame (void) {
	// 1) vm_get_victim()을 사용하여 해당 정보를 Frame Table에서 삭제
    lock_acquire(&frame_lock);
	struct frame *victim = vm_get_victim ();
	lock_release(&frame_lock);

	// 2) Swap out: 해당 Page를 Swap Area로 내보냄, 즉 frame 공간을 disk로 내리는 것
	swap_out(victim->page);
	
	return victim;
}

/* palloc() and get frame. If there is no available page, evict the page
 * and return it. This always return valid address. That is, if the user pool
 * memory is full, this function evicts the frame to get the available memory
 * space.*/
/*
	vm_get_frame: palloc_get_page 호출하여 새 Frame를 가져오는 함수
	성공적으로 가져오면 프레임을 할당하고 멤버를 초기화한 후 반환

	palloc_get_page: 하나의 free 페이지를 가져와 커널 가상 주소를 리턴하는 함수
*/
static struct frame *
vm_get_frame (void) {
	struct frame *frame = (struct frame*)malloc(sizeof(struct frame));

	/*
		PAL_USER: 커널 풀 대신 유저 풀에서 메모리를 할당 받기 위함
		유저 풀의 페이지가 부족하면 사용자 프로그램의 페이지가 부족해지지만,
		커널 풀의 페이지가 부족하면 많은 커널 함수들이 메모리를 확보하는 데 문제가 생길 수 있음
	*/
	frame->kva = palloc_get_page(PAL_USER);

    if (frame->kva == NULL) {
        frame = vm_evict_frame();
        frame->page = NULL;

        return frame;
    }

    list_push_back (&frame_table, &frame->frame_elem);
    frame->page = NULL;

	ASSERT (frame != NULL);
	ASSERT (frame->page == NULL);

	return frame;
}

/* Growing the stack. */
/*
	vm_stack_growth: addr에 하나 이상의 익명 페이지를 할당하여 스택 크기를 늘리는 함수

	할당을 처리할 때 addr을 PGSIZE로 반올림해야 함
	스택은 위에서 아래로 쌓이기 때문에 PGSIZE를 빼줌
*/
static void
vm_stack_growth (void *addr UNUSED) {
	struct thread *curr = thread_current();

	if (vm_alloc_page(VM_ANON | VM_MARKER_0, addr, 1)) {
		if (vm_claim_page(addr)) {
			curr->stack_bottom -= PGSIZE;
		}
    }
}

/* Handle the fault on write_protected page */
static bool
vm_handle_wp (struct page *page UNUSED) {
}

/* Return true on success */
/*
	vm_try_handle_fault: Page fault가 Stack growth에 유효한 경우인지 여부를 확인하는 함수
	exception.c의 page_fault에서 호출됨
*/
bool
vm_try_handle_fault (struct intr_frame *f UNUSED, void *addr UNUSED,
		bool user UNUSED, bool write UNUSED, bool not_present UNUSED) {
			
	struct supplemental_page_table *spt UNUSED = &thread_current ()->spt;
	/* TODO: Validate the fault */
	/* TODO: Your code goes here */

	if (!is_user_vaddr(addr)) {
		return false;
	}

	/*
		rsp_stack은 스레드의 유저 스택 포인터
		1) 사용자 모드에서 커널 모드로 전환되는 경우에만 프로세서가 스택 포인터를 저장함
		2) page_fault()에 전달된 intr_frame의 rsp를 읽으면 사용자 스택 포인터가 아닌 정의되지 않은 값이 생성됨
		3) 사용자 모드에서 커널 모드로 처음 전환할 때 rsp를 thread 구조체에 저장하는 것과 같은, 다른 방법이 필요

		즉 커널 영역이라면 현재 스레드의 rsp_stack 값을, 아니면 f의 rsp 값을 사용
	*/
	struct thread *curr = thread_current();
	void *rsp_stack = is_kernel_vaddr(f->rsp) ? curr->rsp_stack : f->rsp;

	if (not_present) {
        if (!vm_claim_page(addr)) {
			/*
				프레임 할당에 실패했을 때, 주소의 범위가 유효한지 확인하고 스택을 키움
				핀토스는 스택 사이즈 1MB로 제한함, 그 사이에 있어야 함
			*/
            if (rsp_stack - 8 <= addr && USER_STACK - 0x100000 <= addr && addr <= USER_STACK) {
                vm_stack_growth(curr->stack_bottom - PGSIZE);
                return true;
            }
            return false;

        } else {
			return true;
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
/*
	vm_claim_page: 프레임을 페이지에 할당하는 함수
*/
bool
vm_claim_page (void *va UNUSED) {
	struct page *page;
	/* TODO: Fill this function */
	page = spt_find_page(&thread_current()->spt, va);

	if (page == NULL) {
		return false;
	}

	return vm_do_claim_page (page);
}

/* Claim the PAGE and set up the mmu. */
/*
	vm_do_claim_page: 실제로 프레임을 페이지에 할당하는 함수

	Page는 User Page에 있고, Frame은 Kernel Page에 존재함
	가상 메모리 주소(Page)와 물리 메모리 주소(Frame)를 매핑
*/
static bool
vm_do_claim_page (struct page *page) {
	struct thread *curr = thread_current();
	struct frame *frame = vm_get_frame ();
	if (frame == NULL) {
		return false;
	}

	/* Set links */
	frame->page = page;
	page->frame = frame;

	/* TODO: Insert page table entry to map page's VA to frame's PA. */
	/*
		Page의 Writable
		: true면 사용자 프로세스가 page를 수정할 수 있음
		: false면 read-only
	*/

	/* Verify that there's not already a page at that virtual
	 * address, then map our page there. */

	// 유저페이지가 이미 매핑되었거나 메모리 할당에 실패했다면, false를 반환할 것
	bool success = (pml4_get_page (curr->pml4, page->va) == NULL 
					&& pml4_set_page (curr->pml4, page->va, frame->kva, page->writable));

	if (success) {
        return swap_in(page, frame->kva);
    }

    return false;
}

/* Initialize new supplemental page table */
void
supplemental_page_table_init (struct supplemental_page_table *spt UNUSED) {
	hash_init (&spt->spt_hash, page_hash, page_less, NULL);
}

/* Copy supplemental page table from src to dst */
/*
	supplemental_page_table_copy: src에서 dst로 SPT을 복사하는 함수
	
	자식이 부모의 실행 context를 상속해야 할 때 사용됨 ex) fork()
	src의 SPT에 있는 각 페이지를 반복하여 dst의 SPT에 있는 엔트리의 복사본을 만듦
	uninit 페이지를 할당하고 즉시 claim해야 함
*/
bool
supplemental_page_table_copy (struct supplemental_page_table *dst UNUSED, struct supplemental_page_table *src UNUSED) {
	struct hash_iterator iterator;
	hash_first(&iterator, &src->spt_hash);

	while (hash_next(&iterator)) {
		/*
			hash_cur: 현재 elem을 리턴하거나, table의 끝인 null 포인터를 반환하거나
			즉 현재 해시 테이블의 elem을 리턴함
		*/
		struct page *parent_page = hash_entry(hash_cur(&iterator), struct page, hash_elem);

		enum vm_type type = page_get_type(parent_page);
		void *upage = parent_page->va;
		bool writable = parent_page->writable;

		vm_initializer *init = parent_page->uninit.init;
		void *aux = parent_page->uninit.aux;

		if (parent_page->uninit.type & VM_MARKER_0) {
			struct thread *curr = thread_current();
			setup_stack(&curr->tf);

		// 부모의 페이지 타입이 uninit인 경우
		} else if (parent_page->operations->type == VM_UNINIT) {
			if (!vm_alloc_page_with_initializer(type, upage, writable, init, aux)) {
				return false;
			}

		// 부모의 페이지 타입이 uninit이 아니면 spt 추가만
		} else {
			if (!vm_alloc_page(type, upage, writable) || !vm_claim_page(upage)) {
				return false;
			}
		}

		if (parent_page->operations->type != VM_UNINIT) {
			struct page* child_page = spt_find_page(dst, upage);
			memcpy(child_page->frame->kva, parent_page->frame->kva, PGSIZE);
		}
	}

	return true;
}

/* Free the resource hold by the supplemental page table */
/*
	supplemental_page_table_kill: SPT가 보유한 모든 리소스를 해제시키는 함수
	
	프로세스가 종료될 때(process.c의 process_exit) 호출됨
	페이지 엔트리를 반복하여 테이블의 페이지에 대해 destroy(page)를 호출해야 함
	실제 페이지 테이블(pml4)과 물리 메모리(palloced memory)를 걱정할 필요 없음, caller가 이를 정리함
*/
void
supplemental_page_table_kill (struct supplemental_page_table *spt UNUSED) {
	/* TODO: Destroy all the supplemental_page_table hold by thread and
	 * TODO: writeback all the modified contents to the storage. */

	struct hash_iterator iterator;
    hash_first(&iterator, &spt->spt_hash);

    while (hash_next(&iterator)) {
		// hash_cur: 현재 elem을 리턴하거나, table의 끝인 null 포인터를 반환하거나
        struct page *page = hash_entry(hash_cur(&iterator), struct page, hash_elem);

        if (page->operations->type == VM_FILE) {
            do_munmap(page->va);
        }
		free(page);
    }
    hash_destroy(&spt->spt_hash, spt_destroy);
}

// Helper Functions

/*
	hash_entry: 해당 hash_elem을 가지고 있는 page를 리턴하는 함수
	page_bytes: 해당 page의 가상 주소를 hashed index로 변환하는 함수
*/
unsigned page_hash (struct hash_elem *elem, void *aux UNUSED) {
	struct page *page = hash_entry(elem, struct page, hash_elem);

	return hash_bytes(&page->va, sizeof(page->va));
}

/*
	page_less: 두 page의 주소값을 비교하여 왼쪽 값이 작으면 True 리턴하는 함수
*/
bool page_less (struct hash_elem *elema, struct hash_elem *elemb, void *aux UNUSED) {
	struct page *pagea = hash_entry(elema, struct page, hash_elem);
	struct page *pageb = hash_entry(elemb, struct page, hash_elem);

	return pagea->va < pageb->va;
}

/*
	page_insert: hash에 page를 삽입하는 함수, hash_insert() 사용
	해당 자리에 값이 있으면 삽입 실패
*/
bool page_insert (struct hash *hash, struct page *page) {
	if (!hash_insert(hash, &page->hash_elem)) {
		return true;
	} else {
		return false;
	}
}

/*
	page_delete: hash에 page를 삭제하는 함수 (hash, page), hash_delete() 사용
	해당 자리에 값이 없으면 삭제 실패
*/
bool page_delete (struct hash *hash, struct page *page) {
	if (!hash_delete(hash, &page->hash_elem)) {
		return true;
	} else {
		return false;
	}
}

void spt_destroy (struct hash_elem *e, void *aux UNUSED) {
    struct page *page = hash_entry(e, struct page, hash_elem);

    free(page);
}