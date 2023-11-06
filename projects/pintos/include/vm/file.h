#ifndef VM_FILE_H
#define VM_FILE_H
#include "filesys/file.h"
#include "vm/vm.h"

struct page;
enum vm_type;

/*
    Project 3: Swap In/Out
    
    File-backed page는 파일로부터 불러오기 때문에, 매핑된 파일은 백업 저장소 역할
    즉 이 페이지를 제거하면 해당 페이지가 매핑된 파일에 쓰여짐
*/
struct file_page {
    struct file *file;
    struct list_elem file_elem;
};

void vm_file_init (void);
bool file_backed_initializer (struct page *page, enum vm_type type, void *kva);
void *do_mmap(void *addr, size_t length, int writable, struct file *file, off_t offset);
void do_munmap (void *va);
#endif
