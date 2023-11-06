#ifndef VM_ANON_H
#define VM_ANON_H
#include "devices/disk.h"
#include "vm/vm.h"
struct page;
enum vm_type;

/*
    Project 3: Anonymous Page

    Anonymous mapping에는 backing file이나 device가 없음
    File-backed page와 달리 이름이 지정된 파일 소스가 없기 때문에 'Anonymous'라고 함
    Anonymous page는 스택 및 힙과 같은 실행 파일에 사용됨
*/
struct anon_page {
    disk_sector_t index;
    struct thread *thread;
};

void vm_anon_init (void);
bool anon_initializer (struct page *page, enum vm_type type, void *kva);

#endif
