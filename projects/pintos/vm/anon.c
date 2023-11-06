/* anon.c: Implementation of page for non-disk image (a.k.a. anonymous page). */

#include <bitmap.h>
#include "vm/vm.h"
#include "devices/disk.h"
#include "threads/mmu.h"

/* DO NOT MODIFY BELOW LINE */
static struct disk *swap_disk;
static bool anon_swap_in (struct page *page, void *kva);
static bool anon_swap_out (struct page *page);
static void anon_destroy (struct page *page);

/*
	Project 3: Swap In/Out

	swap_table: 스왑 디스크에서 사용 가능한 영역과 사용된 영역을 관리하기 위함
	비트가 0이면 해당 페이지를 사용 가능한 영역으로 선정
	1이면 참조 비트를 0으로 재설정하고 이때 변경 내용을 항상 디스크에 저장

	SECTORS_PER_PAGE: 스왑 영역은 PGSIZE 단위로 관리됨
	섹터(Sector)는 하드 드라이브의 최소 기억 단위
	이를 페이지 단위로 관리하려면 섹터 단위를 페이지 단위로 변경해야 함
	이게 SECTORS_PER_PAGE, 즉 8섹터 당 1페이지를 뜻함
*/
struct bitmap *swap_table;
static struct lock bitmap_lock;

extern struct lock frame_lock;

/* DO NOT MODIFY this struct */
static const struct page_operations anon_ops = {
	.swap_in = anon_swap_in,
	.swap_out = anon_swap_out,
	.destroy = anon_destroy,
	.type = VM_ANON,
};

/* Initialize the data for anonymous pages */
/*
	vm_anon_init: Anonymous page의 subsystem을 초기화하는 함수
	해당 함수에서 익명 페이지와 관련된 어떤 것이든 설정할 수 있음
*/
void
vm_anon_init (void) {
	/* TODO: Set up the swap_disk. */
	// disk_get(1, 1): 스왑 디스크를 얻는다는 의미
	swap_disk = disk_get(1, 1);
    size_t swap_size = disk_size(swap_disk);
	
	// bitmap_create: 모든 비트들을 false로 초기화함
    swap_table = bitmap_create(swap_size);
	lock_init(&bitmap_lock);
}

/* Initialize the file mapping */
/*
	anon_initializer: Anonymous page의 이니셜라이저, Anonymous page의 handler를 설정하는 함수
	모든 페이지는 커널이 Page fault를 인터셉트할 때만 lazy-loaded 되어야 함

	파일을 처음 가상 페이지에 로드할 때, load_segment() -> vm_alloc_page_with_initializer()에서 실행됨
*/
bool
anon_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	page->operations = &anon_ops;

	struct anon_page *anon_page = &page->anon;
	anon_page->index = -1;
	anon_page->thread = thread_current();
}

/* Swap in the page by read contents from the swap disk. */
/*
	anon_swap_in: 디스크에서 메모리로 데이터 내용을 읽어서 스왑 디스크에서 익명 페이지로 스왑하는 함수
	데이터의 위치는 페이지가 스왑 아웃될 때 페이지 구조에 스왑 디스크가 저장되어 있어야 한다는 것, 스왑 테이블 업데이트해야 함

	Swap out된 page에 저장된 index 값으로 Swap slot을 찾아, 해당 slot에 저장된 data를 다시 memory에 복원시킴
	즉 Disk to Memory (Kernel Virtual Address)
*/
static bool
anon_swap_in (struct page *page, void *kva) {
	struct anon_page *anon_page = &page->anon;

	// index: Swap out된 페이지가 Disk swap 영역 어느 위치에 저장되었는지
	disk_sector_t index = anon_page->index;

	if (index == -1) {
		return false;
	}

	// bitmap_contains: swap_table의 해당 범위에 false가 포함되었는지, 즉 유효한 index인지 확인
	lock_acquire(&bitmap_lock);
    bool check = bitmap_contains(swap_table, index, 8, false);
    lock_release(&bitmap_lock);

    if (check) {
        return false;
    }

    for (int i = 0; i < 8; i++) {
		// disk_read: 해당 swap 영역의 데이터를 읽어, 가상 주소 공간(kva)에 씀
        disk_read(swap_disk, index + i, kva + DISK_SECTOR_SIZE * i);
    }

	// bitmap_set_multiple: 해당 Swap slot을 false로 만들어 다음에 사용할 수 있도록 함
    lock_acquire(&bitmap_lock);
    bitmap_set_multiple(swap_table, index, 8, false);
    lock_release(&bitmap_lock);
    
    return true;
}

/* Swap out the page by writing contents to the swap disk. */
/*
	anon_swap_out: 메모리에서 디스크로 내용을 복사하여 익명 페이지를 스왑 디스크로 교체하는 함수
	먼저 스왑 테이블을 사용하여 디스크에서 사용 가능한 스왑 슬롯을 찾은 다음 데이터 페이지를 슬롯에 복사함
	데이터의 위치는 페이지 구조체에 저장되어야 함, 디스크에 사용 가능한 슬롯이 더 이상 없으면 커널 패닉이 발생할 수 있음

	Disk 상의 Swap disk 공간에 임시로 해당 page를 저장, 즉 Memory (Kernel Virtual Address) to Disk
*/
static bool
anon_swap_out (struct page *page) {
	struct anon_page *anon_page = &page->anon;

	/*
		bitmap_scan_and_flip: swap_table에서 할당 받을 수 있는 Swap slot을 찾음
		찾고 비트를 반전 시킴 -> 사용할 거니까
	*/
    lock_acquire(&bitmap_lock);
    disk_sector_t index = (disk_sector_t)bitmap_scan_and_flip(swap_table, 0, 8, false);
    lock_release(&bitmap_lock);

    if (index == BITMAP_ERROR) {
        return false;
    }

    anon_page->index = index;

    for (int i = 0; i < 8; i++) {
		// disk_write: Page의 데이터를 해당 Disk 공간에 씀
        disk_write(swap_disk, index + i, page->frame->kva + i + DISK_SECTOR_SIZE * i);
    }

	// Page에서 Disk로 Out 됐으니, 해당 Page 정리해야지
	pml4_clear_page(anon_page->thread->pml4, page->va);
    pml4_set_dirty(anon_page->thread->pml4, page->va, false);
    page->frame = NULL;

    return true;
}

/* Destroy the anonymous page. PAGE will be freed by the caller. */
/*
	anon_destroy: Anon Page가 보유한 리소스를 해제하는 함수
	
	Page free할 필요 없음, Caller 해제하기 때문
	-> Frame만 처리하면 됨
*/
static void
anon_destroy (struct page *page) {
	struct anon_page *anon_page = &page->anon;
	disk_sector_t index = anon_page->index;

	if (page->frame != NULL) {
		lock_acquire(&frame_lock);
		list_remove(&page->frame->frame_elem);
		lock_release(&frame_lock);

		free(page->frame);
	}

	if (index != -1) {
		bitmap_set_multiple(swap_table, index, 8, false);
	}
}
