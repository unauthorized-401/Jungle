/* file.c: Implementation of memory backed file object (mmaped object). */

#include <string.h>
#include "vm/vm.h"
#include "userprog/process.h"
#include "threads/mmu.h"

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
/*
    vm_file_init: File-backed page의 하위 시스템을 초기화하는 함수
    이 함수에서 파일 백업 페이지와 관련된 모든 것을 설정할 수 있음
*/
void
vm_file_init (void) {
}

/* Initialize the file backed page */
/*
    file_backed_initializer: File-backed page를 초기화하는 함수
    File-backed page에 대한 핸들러를 설정함
*/
bool
file_backed_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	page->operations = &file_ops;

	struct file_page *file_page = &page->file;
}

/* Swap in the page by read contents from the file. */
/*
    file_backed_swap_in: 파일에서 내용을 읽어 kva에서 페이지를 교체하는 함수
    파일 시스템과 동기화해야 함
*/
static bool
file_backed_swap_in (struct page *page, void *kva) {
	if (page == NULL) {
        return false;
    }

	struct file_page *file_page UNUSED = &page->file;

    struct container *aux = (struct container *)page->uninit.aux;

    struct file *file = aux->file;
	off_t offset = aux->offset;
    size_t page_read_bytes = aux->read_bytes;
    size_t page_zero_bytes = PGSIZE - page_read_bytes;

    // file_seek: 파일의 시작 부분을 offset으로 설정함
	file_seek (file, offset);

    // file_read: file에서 page_read_bytes만큼 읽어 kva에 씀
    if (file_read (file, kva, page_read_bytes) != (int) page_read_bytes) {
        return false;
    }

    // memset: 쓰고 난 나머지 부분(page_zero_bytes), 0으로 채워줌
    memset (kva + page_read_bytes, 0, page_zero_bytes);

    return true;
}

/* Swap out the page by writeback contents to the file. */
/*
    file_backed_swap_out: 내용을 다시 파일에 기록하여 페이지를 교체하는 함수
    페이지가 dirty한지 확인 필요, dirty하지 않으면 파일의 내용을 수정할 페이지 없음
    페이지 교체 후 페이지의 dirty bit를 off해야 함
*/
static bool
file_backed_swap_out (struct page *page) {
    if (page == NULL) {
        return false;
    }

	struct file_page *file_page UNUSED = &page->file;

    struct container *aux = (struct container *) page->uninit.aux;
    
    /*
        사용 되었던 페이지(dirty page)인지 체크
        dirty하다면, 파일의 내용을 업데이트해주어야 함
    */
    if (pml4_is_dirty(thread_current()->pml4, page->va)) {
        file_write_at(aux->file, page->va, aux->read_bytes, aux->offset);
        pml4_set_dirty (thread_current()->pml4, page->va, 0);
    }

	// pml4_clear_page: 프레임에 있던 해당 페이지를 clear 시킴
    pml4_clear_page(thread_current()->pml4, page->va);
}

/* Destory the file backed page. PAGE will be freed by the caller. */
/*
    file_backed_destroy: 관련 파일을 닫아, File-backed page를 destroy하는 함수
    페이지 구조를 해제할 필요 없음, caller가 이 함수를 호출할 것임
*/
static void
file_backed_destroy (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;
    list_remove(&file_page->file_elem);

    if (page->frame != NULL) {
        list_remove(&page->frame->frame_elem);
        free(page->frame);
    }
}

/* Do the mmap */
/*
    do_mmap: offset 바이트에서 시작하여 fd로 열린 파일을 프로세스의 가상 주소 공간인 addr에 length 바이트로 매핑

	전체 파일은 addr에서 시작하는 연속적인 가상 페이지로 매핑됨
	파일 길이가 PGSIZE의 배수가 아닌 경우, 최종 매핑된 페이지의 일부 바이트가 파일 끝을 넘어 '튀어나오게' 됨
    -> 즉 offset을 이용하여 PGSIZE의 배수가 되도록 구현하라는 의미

	페이지에 오류가 발생하면 이 바이트를 0으로 설정하고 페이지가 디스크에 다시 기록될 때 이 바이트를 삭제함

	성공하면 이 함수는 파일이 매핑된 가상 주소를 반환, 실패하면 파일을 매핑할 수 있는 유효한 주소가 아닌 NULL을 반환
*/
void *
do_mmap (void *addr, size_t length, int writable, struct file *file, off_t offset) {
    struct file *file_copy = file_reopen(file);
    if (file_copy == NULL) {
        return NULL;
    }

    void *origin_addr = addr;

    size_t read_bytes = length > file_length(file) ? file_length(file) : length;
    size_t zero_bytes = PGSIZE - read_bytes % PGSIZE;

	while (read_bytes > 0 || zero_bytes > 0) {
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

        struct container *container = (struct container*)malloc(sizeof(struct container));
        container->file = file_copy;
        container->offset = offset;
        container->read_bytes = page_read_bytes;

		if (!vm_alloc_page_with_initializer (VM_FILE, addr, writable, lazy_load_segment, container)) {
			return NULL;
        }

		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		addr += PGSIZE;
		offset += page_read_bytes;
	}

	return origin_addr;
}

/* Do the munmap */
/*
	do_munmap: addr 주소 범위에 대한 매핑을 해제시키는 함수

	addr은 아직 매핑 해제되지 않은 동일한 프로세스에서 mmap에 대한 이전 호출에서 반환된 가상 주소여야 함
*/
void do_munmap (void *addr) {
    while (true) {
        struct thread *curr = thread_current();
        struct page* page = spt_find_page(&curr->spt, addr);
        
        if (page == NULL) {
            break;
        }

        struct container *container = (struct container *) page->uninit.aux;
        
        /*
            파일을 close하거나 remove해도 매핑된 건 해제되지 않음
            일단 매핑이 되면 munmap이 호출되거나 프로세스가 종료될 때까지 유효함
            file_reopen()를 사용하여 파일의 각각 매핑에 대해, 분리되고 독립적인 참조를 얻어야 함
            
            두 개 이상의 프로세스가 같은 파일을 매핑하는 경우, 일관된 데이터를 볼 필요 없음
            mmap에서 페이지가 공유되는지 아닌지 지정할 수 있음
        */
        if (pml4_is_dirty(curr->pml4, page->va)) {
            file_write_at(container->file, addr, container->read_bytes, container->offset);
            pml4_set_dirty (curr->pml4, page->va, 0);
        }

        pml4_clear_page(curr->pml4, page->va);
        addr += PGSIZE;
    }
}
