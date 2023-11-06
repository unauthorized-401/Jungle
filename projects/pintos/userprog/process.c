#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/mmu.h"
#include "threads/vaddr.h"
#include "intrinsic.h"
#ifdef VM
#include "vm/vm.h"
#endif

static void process_cleanup (void);
static bool load (const char *file_name, struct intr_frame *if_);
static void initd (void *f_name);
static void __do_fork (void *);

void push_arguments(int argc, char **argv, struct intr_frame *if_);
struct thread *find_child(int pid);

/* General process initializer for initd and other process. */
// process_init: 현재 프로세스를 초기화하는 함수
static void
process_init (void) {
	struct thread *current = thread_current ();
}

/* Starts the first userland program, called "initd", loaded from FILE_NAME.
 * The new thread may be scheduled (and may even exit)
 * before process_create_initd() returns. Returns the initd's
 * thread id, or TID_ERROR if the thread cannot be created.
 * Notice that THIS SHOULD BE CALLED ONCE. */
/*
	process_create_initd: 명령줄에서 받은 arguments를 통해 실행하고자 하는 파일에 대한 프로세스를 생성
	여기서 파라미터로 받는 file_name은 arg[1]로 받은 파일명
*/
tid_t
process_create_initd (const char *file_name) {
	char *fn_copy;
	tid_t tid;

	/* Make a copy of FILE_NAME.
	 * Otherwise there's a race between the caller and load(). */
	/*
		palloc_get_page: 물리 프레임을 바로 할당시켜주는 함수
		메모리에서 4KB 만큼 물리메모리 공간을 잡고 시작 주소를 리턴해줌
	*/
	fn_copy = palloc_get_page (0);
	if (fn_copy == NULL)
		return TID_ERROR;
	strlcpy (fn_copy, file_name, PGSIZE);

	/* Create a new thread to execute FILE_NAME. */
	/*
		thread_create: file_name의 이름으로 PRI_DEFAULT를 우선순위 값으로 가지는 새로운 스레드 생성
		Pintos에서는 단일 스레드만 고려, 새로운 스레드 생성 후 TID 리턴
	*/

	char *save_ptr;
	
	file_name = strtok_r (file_name, " ", &save_ptr);

	/*
		thread_create()를 통해 만들어진 스레드에게 initd 함수를 실행하라는 의미
		여기서 initd 함수의 인자는 fn_copy

		thread_create() + "file_name"
		initd() + "fn_copy"
	*/
	tid = thread_create (file_name, PRI_DEFAULT, initd, fn_copy);

	if (tid == TID_ERROR)
		palloc_free_page (fn_copy);
	return tid;
}

/* A thread function that launches first user process. */
// initd: 프로세스 초기화, process_exec() 함수 주목
static void
initd (void *f_name) {
#ifdef VM
	supplemental_page_table_init (&thread_current ()->spt);
#endif

	/*
		process_init: 현재 프로세스를 초기화하는 함수
		현재 실행중인 스레드가 있는지 확인하는 것 자체만으로도, 프로세스를 초기화하는 효과
	*/
	process_init ();

	/*
		process_exec: 실제로 사용자 프로그램을 실행시키는 함수
		argument passing -> load() -> do_iret() 진행
	*/
	if (process_exec (f_name) < 0)
		PANIC("Fail to launch initd\n");
	NOT_REACHED ();
}

/* Clones the current process as `name`. Returns the new process's thread id, or
 * TID_ERROR if the thread cannot be created. */
tid_t
process_fork (const char *name, struct intr_frame *if_ UNUSED) {
	/* Clone current thread to new thread.*/

	/*
		Project 2: System Call
	*/

	struct thread *curr = thread_current();
    memcpy(&curr->parent_if, if_, sizeof(struct intr_frame));

	/*
		위에서 memcpy를 통해 현재 스레드의 parent_if에 if_를 저장
		: 복사될 스레드가 현재 스레드의 parent_if를 참조하도록, __do_fork()의 memcpy문을 보면 parent_if를 if_로 복사함
		: 여기서 parent_if는 userland의 context를 뜻함
		: 즉, 커널 스택 영역을 가리키고 있기 때문에 유저 영역을 알려줘야 함

		thread_create() + "name"
		__do_fork() + "curr"
	*/
    tid_t pid = thread_create(name, PRI_DEFAULT, __do_fork, curr);

    if (pid == TID_ERROR) {
        return TID_ERROR;
    }

    struct thread *child = find_child(pid);

	/*
		do_fork에서 fork sema up으로 막혀있었는데, fork sema down 해줌
		자식 스레드가 생성되고 do_fork 완료할 때까지 fork()에서 대기한다는 의미
		모두 완료되면, 자식 스레드의 tid를 리턴
	*/
    sema_down(&child->fork_sema);

    if (child->exit_status == -1) {
		return TID_ERROR;
	}

    return pid;
}

#ifndef VM
/* Duplicate the parent's address space by passing this function to the
 * pml4_for_each. This is only for the project 2. */
static bool
duplicate_pte (uint64_t *pte, void *va, void *aux) {
	struct thread *current = thread_current ();
	struct thread *parent = (struct thread *) aux;
	void *parent_page;
	void *newpage;
	bool writable;

	/* 1. TODO: If the parent_page is kernel page, then return immediately. */
	if (is_kernel_vaddr(va)) {
        return true;
    }

	/* 2. Resolve VA from the parent's page map level 4. */
	parent_page = pml4_get_page (parent->pml4, va);
	if (parent_page == NULL) {
        return false;
    }

	/* 3. TODO: Allocate new PAL_USER page for the child and set result to
	 *    TODO: NEWPAGE. */
	newpage = palloc_get_page (PAL_USER | PAL_ZERO);
    if (newpage == NULL) {
        return false;
    }

	/* 4. TODO: Duplicate parent's page to the new page and
	 *    TODO: check whether parent's page is writable or not (set WRITABLE
	 *    TODO: according to the result). */
	// is_writable(): pte가 읽고 쓰기가 가능한지 확인
	memcpy(newpage, parent_page, PGSIZE);
    writable = is_writable(pte);

	/* 5. Add new page to child's page table at address VA with WRITABLE
	 *    permission. */
	if (!pml4_set_page (current->pml4, va, newpage, writable)) {
		/* 6. TODO: if fail to insert page, do error handling. */
		return false;
	}
	return true;
}
#endif

/* A thread function that copies parent's execution context.
 * Hint) parent->tf does not hold the userland context of the process.
 *       That is, you are required to pass second argument of process_fork to
 *       this function. */
static void
__do_fork (void *aux) {
	/*
		parent: 부모 스레드 (thread_create를 호출할 당시의 running thread)
		current: 자식 스레드 (현재의 running thread)

		fork() 시 부모 프로세스는 자식 프로세스의 PID를 리턴하고, 자식 프로세스는 0을 리턴함
		부모의 pml4를 자식의 pml4에 복사해주어야 함
	*/
	struct intr_frame if_;
	struct thread *parent = (struct thread *) aux;
	struct thread *current = thread_current ();
	/* TODO: somehow pass the parent_if. (i.e. process_fork()'s if_) */
	struct intr_frame *parent_if;
	bool succ = true;

	/* 1. Read the cpu context to local stack. */
	parent_if = &parent->parent_if;
	memcpy (&if_, parent_if, sizeof (struct intr_frame));
	if_.R.rax = 0;

	/* 2. Duplicate PT */
	current->pml4 = pml4_create();
	if (current->pml4 == NULL)
		goto error;

	process_activate (current);
#ifdef VM
	supplemental_page_table_init (&current->spt);
	if (!supplemental_page_table_copy (&current->spt, &parent->spt))
		goto error;
#else
	if (!pml4_for_each (parent->pml4, duplicate_pte, parent))
		goto error;
#endif

	/* TODO: Your code goes here.
	 * TODO: Hint) To duplicate the file object, use `file_duplicate`
	 * TODO:       in include/filesys/file.h. Note that parent should not return
	 * TODO:       from the fork() until this function successfully duplicates
	 * TODO:       the resources of parent.*/

	// Project 2: System Call
	if (parent->fd_index >= FDCOUNT_LIMIT) {
		goto error;
	}

	current->fd_table[0] = parent->fd_table[0];
	current->fd_table[1] = parent->fd_table[1];

	for (int i = 2 ; i < FDCOUNT_LIMIT; i++) {
        struct file *f = parent->fd_table[i];

        if (f == NULL) {
            continue;
        }

		// file.c의 file_duplicate() 사용
        current->fd_table[i] = file_duplicate(f);
    }

	current->fd_index = parent->fd_index;

	sema_up(&current->fork_sema);

	/* Finally, switch to the newly created process. */
	if (succ)
		do_iret (&if_);
error:
	// Project 2: System Call
	current->exit_status = TID_ERROR;
    sema_up(&current->fork_sema);
    exit(TID_ERROR);
}

/* Switch the current execution context to the f_name.
 * Returns -1 on fail. */
/*
	process_exec: 사용자가 입력한 명령어를 수행할 수 있도록, 프로그램을 메모리에 적재하고 실행하는 함수
	f_name을 받아 실행 프로그램명과 옵션을 분리시켜줘야 함
*/
int
process_exec (void *f_name) {
	/*
		Project 2: Argument Passing

		파라미터가 왜 void *로 넘어와요?
		: void 포인터는 함수에서 다양한 자료형을 받아들일 때 유용하게 사용, 지금은 문자열로 인식해야 함으로 char *로 변환
	*/

	char *file_name = f_name;
	bool success;

	/* We cannot use the intr_frame in the thread structure.
	 * This is because when current thread rescheduled,
	 * it stores the execution information to the member.
	 * 
	 * thread 구조체에서 intr_frame을 사용할 수 없다.
	 * 왜냐하면 thread_current()가 스케줄링되면, member에 실행 정보를 저장하기 때문이다.
	 * 
	 * 이 process_exec()는 file_name으로 가져온 스레드로 context switching을 하는 함수
	 * 따라서, 현재 스레드의 레지스터를 쓰면 안된다는 의미
	 * 현재 스레드의 레지스터에 context를 저장해야 다시 현재 스레드를 실행할 때 context를 가져와야 함으로
	 * */

	/*
		intr_frame: 인터럽트가 들어왔을 때, 이전에 작업하던 Context를 Switching하기 위한 정보를 담고 있음
		실행중인 프로세스의 레지스터 정보, 스택 포인터, 인스트럭션 카운터를 저장

		ds: data segment
		es: extra segment
		ss: stack segment
		cs: code segment
		eflags: cpu flags

		SEL_UDSEG: 유저 데이터 영역
		SEL_UCSEG: 유저 코드 영역
	*/
	struct intr_frame _if;
	_if.ds = _if.es = _if.ss = SEL_UDSEG;
	_if.cs = SEL_UCSEG;
	_if.eflags = FLAG_IF | FLAG_MBS;

	/* We first kill the current context */
	/*
		process_cleanup: 현재 실행중인 스레드의 Page Directory, Swtich information을 지워줌
		가상 메모리와 물리 메모리를 연결해주는 pml4를 NULL로 변경, 연결을 끊어줌

		새로 생성한 프로세스를 실행하기 위해 현재 실행 중인 스레드와 Context Switching을 하기 위한 준비
	*/
	process_cleanup ();

	/* And then load the binary */
	/*
		load: 현재 프로세스에 해당 파일을 로드시켜줌, File Parsing 작업을 여기서 진행해야 함
		파일 로드가 성공적으로 된다면 do_iret(), NOT_REACHED()를 통해 생성된 프로세스로 Context Switching하면 됨
	*/
	success = load (file_name, &_if);

	/* If load failed, quit. */
	palloc_free_page (file_name);

	if (!success) {
		return -1;
	}

	/* Start switched process. */
	/*
		do_iret
		: 현재까지 작업했던 인터럽트 프레임(tf)의 값들을 레지스터에 넣는 작업을 수행함
	*/
	do_iret (&_if);
	NOT_REACHED ();
}


/* Waits for thread TID to die and returns its exit status.  If
 * it was terminated by the kernel (i.e. killed due to an
 * exception), returns -1.  If TID is invalid or if it was not a
 * child of the calling process, or if process_wait() has already
 * been successfully called for the given TID, returns -1
 * immediately, without waiting.
 *
 * This function will be implemented in problem 2-2.  For now, it
 * does nothing. */
int
process_wait (tid_t child_tid UNUSED) {
	/* XXX: Hint) The pintos exit if process_wait (initd), we recommend you
	 * XXX:       to add infinite loop here before
	 * XXX:       implementing the process_wait. */

	struct thread *child_thread = find_child(child_tid);

    if (child_thread == NULL) {
        return -1;
    }

	/*
		부모는 자식 프로세스가 종료될 때까지 대기, 정상적으로 종료 시 exit_status 반환, 아니면 -1 반환

		1) wait sema down: 부모는 자식이 process_exit()에서 wait sema up할 때까지 대기
		2) remove child elem: 부모가 exit_status를 가져올 수 있도록 자식의 페이지는 유지
		3) free sema up: 잠들었던 자식 프로세스 깨움
		4) 부모 프로세스의 wait() 종료 -> 자식 프로세스의 process_exit() 종료
	*/
    sema_down(&child_thread->wait_sema);
    list_remove(&child_thread->child_elem);
    sema_up(&child_thread->free_sema);

    return child_thread->exit_status;
}

/* Exit the process. This function is called by thread_exit (). */
void
process_exit (void) {
	struct thread *curr = thread_current ();
	/* TODO: Your code goes here.
	 * TODO: Implement process termination message (see
	 * TODO: project2/process_termination.html).
	 * TODO: We recommend you to implement process resource cleanup here. */

	// 현재 열려있는 모든 파일을 닫음
	for (int i = 0; i < FDCOUNT_LIMIT; i++) {
        close(i);
    }

    palloc_free_multiple(curr->fd_table, FDT_PAGES);

    sema_up(&curr->wait_sema);
    file_close(curr->running);
    sema_down(&curr->free_sema);

	process_cleanup ();
}

/* Free the current process's resources. */
static void
process_cleanup (void) {
	struct thread *curr = thread_current ();

#ifdef VM
	// spt에 데이터가 있을 경우에만 kill 시키도록 수정
	if (!hash_empty(&curr->spt.spt_hash)) {
		supplemental_page_table_kill (&curr->spt);
	}
#endif

	uint64_t *pml4;
	/* Destroy the current process's page directory and switch back
	 * to the kernel-only page directory. */
	pml4 = curr->pml4;
	if (pml4 != NULL) {
		/* Correct ordering here is crucial.  We must set
		 * cur->pagedir to NULL before switching page directories,
		 * so that a timer interrupt can't switch back to the
		 * process page directory.  We must activate the base page
		 * directory before destroying the process's page
		 * directory, or our active page directory will be one
		 * that's been freed (and cleared). */
		curr->pml4 = NULL;
		pml4_activate (NULL);
		pml4_destroy (pml4);
	}
}

/* Sets up the CPU for running user code in the nest thread.
 * This function is called on every context switch. */
void
process_activate (struct thread *next) {
	/* Activate thread's page tables. */
	pml4_activate (next->pml4);

	/* Set thread's kernel stack for use in processing interrupts. */
	tss_update (next);
}

/* We load ELF binaries.  The following definitions are taken
 * from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
#define EI_NIDENT 16

#define PT_NULL    0            /* Ignore. */
#define PT_LOAD    1            /* Loadable segment. */
#define PT_DYNAMIC 2            /* Dynamic linking info. */
#define PT_INTERP  3            /* Name of dynamic loader. */
#define PT_NOTE    4            /* Auxiliary info. */
#define PT_SHLIB   5            /* Reserved. */
#define PT_PHDR    6            /* Program header table. */
#define PT_STACK   0x6474e551   /* Stack segment. */

#define PF_X 1          /* Executable. */
#define PF_W 2          /* Writable. */
#define PF_R 4          /* Readable. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
 * This appears at the very beginning of an ELF binary. */
struct ELF64_hdr {
	unsigned char e_ident[EI_NIDENT];
	uint16_t e_type;
	uint16_t e_machine;
	uint32_t e_version;
	uint64_t e_entry;
	uint64_t e_phoff;
	uint64_t e_shoff;
	uint32_t e_flags;
	uint16_t e_ehsize;
	uint16_t e_phentsize;
	uint16_t e_phnum;
	uint16_t e_shentsize;
	uint16_t e_shnum;
	uint16_t e_shstrndx;
};

struct ELF64_PHDR {
	uint32_t p_type;
	uint32_t p_flags;
	uint64_t p_offset;
	uint64_t p_vaddr;
	uint64_t p_paddr;
	uint64_t p_filesz;
	uint64_t p_memsz;
	uint64_t p_align;
};

/* Abbreviations */
#define ELF ELF64_hdr
#define Phdr ELF64_PHDR

bool setup_stack (struct intr_frame *if_);
static bool validate_segment (const struct Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes,
		bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
 * Stores the executable's entry point into *RIP
 * and its initial stack pointer into *RSP.
 * Returns true if successful, false otherwise. */
/*
	RIP = PC: 프로그램 카운터, 실행할 프로그램을 가리킴
	RSP: 스택 포인터의 끝을 가리킴

	load: 현재 프로세스에 해당 파일을 로드시켜줌, File Parsing 작업을 여기서 진행해야 함
*/
static bool
load (const char *file_name, struct intr_frame *if_) {
	struct thread *t = thread_current ();
	struct ELF ehdr;
	struct file *file = NULL;
	off_t file_ofs;
	bool success = false;
	int i;

	/*
		File Parsing
		: strtok()은 multi-thread 프로그램에서 오류를 유발할 수 있음
		: thread-safe한 strtok_r()을 사용하는 것을 권유

		strtok_r()
		: 파싱한 첫번째 문자열만 반환하기 때문에 while으로 NULL이 나올 때까지 반복해주어야 함

		왜 128로 선언하나요?
		: Pintos 가이드에서 명령어 제한 길이는 128 바이트라고 언급됨
	*/
	char *wordptr, *saveptr;

	int argc = 0;
	char *argv[128];

	wordptr = strtok_r(file_name, " ", &saveptr);
	argv[argc] = wordptr;

	while (wordptr != NULL) {
		wordptr = strtok_r(NULL, " ", &saveptr);
		argc += 1;
		argv[argc] = wordptr;
	}

	/* Allocate and activate page directory. */
	t->pml4 = pml4_create ();
	if (t->pml4 == NULL)
		goto done;
	process_activate (thread_current ());

	/* Open executable file. */
	file = filesys_open (file_name);
	if (file == NULL) {
		printf ("load: %s: open failed\n", file_name);
		goto done;
	}

	/*
		Project 2: System Call

		1) 현재 스레드가 할 일 설정
		2) 해당 파일에 다른 내용을 쓸 수 없도록 함

		file.c의 file_deny_write() 사용
	*/
	t->running = file;
	file_deny_write(file);

	/* Read and verify executable header. */
	/*
		ELF: 실행 파일, 목적 파일, 공유 라이브러리 그리고 코어 덤프를 위한 표준 파일 형식
		해당 파일의 헤더를 체크하여 해당 형식에 적합한지를 (실행 가능한 파일인지) 확인
	*/
	if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
			|| memcmp (ehdr.e_ident, "\177ELF\2\1\1", 7)
			|| ehdr.e_type != 2
			|| ehdr.e_machine != 0x3E // amd64
			|| ehdr.e_version != 1
			|| ehdr.e_phentsize != sizeof (struct Phdr)
			|| ehdr.e_phnum > 1024) {
		printf ("load: %s: error loading executable\n", file_name);
		goto done;
	}

	/* Read program headers. */
	file_ofs = ehdr.e_phoff;
	for (i = 0; i < ehdr.e_phnum; i++) {
		struct Phdr phdr;

		if (file_ofs < 0 || file_ofs > file_length (file))
			goto done;
		file_seek (file, file_ofs);

		if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
			goto done;
		file_ofs += sizeof phdr;
		switch (phdr.p_type) {
			case PT_NULL:
			case PT_NOTE:
			case PT_PHDR:
			case PT_STACK:
			default:
				/* Ignore this segment. */
				break;
			case PT_DYNAMIC:
			case PT_INTERP:
			case PT_SHLIB:
				goto done;
			case PT_LOAD:
				if (validate_segment (&phdr, file)) {
					bool writable = (phdr.p_flags & PF_W) != 0;
					uint64_t file_page = phdr.p_offset & ~PGMASK;
					uint64_t mem_page = phdr.p_vaddr & ~PGMASK;
					uint64_t page_offset = phdr.p_vaddr & PGMASK;
					uint32_t read_bytes, zero_bytes;
					if (phdr.p_filesz > 0) {
						/* Normal segment.
						 * Read initial part from disk and zero the rest. */
						read_bytes = page_offset + phdr.p_filesz;
						zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
								- read_bytes);
					} else {
						/* Entirely zero.
						 * Don't read anything from disk. */
						read_bytes = 0;
						zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
					}
					/*
						load_segment
						: 커널 주소에 해당 파일을 로드함
						: install_page()를 통해 유저 스택 페이지와 커널 주소를 매핑시킴

						즉, load_segment()가 호출되면 해당 파일이 전부 커널 페이지에 올라가는 것
					*/
					if (!load_segment (file, file_page, (void *) mem_page,
								read_bytes, zero_bytes, writable))
						goto done;
				}
				else
					goto done;
				break;
		}
	}

	/* Set up stack. */
	/*
		setup_stack
		: 최소 크기의 스택을 생성하는데, 이는 0으로 초기화된 한 페이지를 유저 스택으로 할당
	*/
	if (!setup_stack (if_))
		goto done;

	/* Start address. */
	if_->rip = ehdr.e_entry;

	/* TODO: Your code goes here.
	 * TODO: Implement argument passing (see project2/argument_passing.html). */
	/*
		Argument들을 Stack에 올리는 과정이 필요, 과정이 길어 해당 함수를 새로 생성함
		인터럽트 프레임을 스택에 올리는 것은 아니고 인터럽트 프레임 내 구조체 중 "rsp"에 값을 넣어주기 위함
		이후 do_iret()에서 이 인터럽트 프레임을 스택에 올림
	*/
	push_arguments(argc, argv, if_);

	success = true;

done:
	/* We arrive here whether the load is successful or not. */
	return success;
}


/* Checks whether PHDR describes a valid, loadable segment in
 * FILE and returns true if so, false otherwise. */
static bool
validate_segment (const struct Phdr *phdr, struct file *file) {
	/* p_offset and p_vaddr must have the same page offset. */
	if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK))
		return false;

	/* p_offset must point within FILE. */
	if (phdr->p_offset > (uint64_t) file_length (file))
		return false;

	/* p_memsz must be at least as big as p_filesz. */
	if (phdr->p_memsz < phdr->p_filesz)
		return false;

	/* The segment must not be empty. */
	if (phdr->p_memsz == 0)
		return false;

	/* The virtual memory region must both start and end within the
	   user address space range. */
	if (!is_user_vaddr ((void *) phdr->p_vaddr))
		return false;
	if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
		return false;

	/* The region cannot "wrap around" across the kernel virtual
	   address space. */
	if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
		return false;

	/* Disallow mapping page 0.
	   Not only is it a bad idea to map page 0, but if we allowed
	   it then user code that passed a null pointer to system calls
	   could quite likely panic the kernel by way of null pointer
	   assertions in memcpy(), etc. */
	if (phdr->p_vaddr < PGSIZE)
		return false;

	/* It's okay. */
	return true;
}

#ifndef VM
/* Codes of this block will be ONLY USED DURING project 2.
 * If you want to implement the function for whole project 2, implement it
 * outside of #ifndef macro. */

/* load() helpers. */
static bool install_page (void *upage, void *kpage, bool writable);

/* Loads a segment starting at offset OFS in FILE at address
 * UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
 * memory are initialized, as follows:
 *
 * - READ_BYTES bytes at UPAGE must be read from FILE
 * starting at offset OFS.
 *
 * - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.
 *
 * The pages initialized by this function must be writable by the
 * user process if WRITABLE is true, read-only otherwise.
 *
 * Return true if successful, false if a memory allocation error
 * or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes, bool writable) {
	ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
	ASSERT (pg_ofs (upage) == 0);
	ASSERT (ofs % PGSIZE == 0);

	file_seek (file, ofs);
	while (read_bytes > 0 || zero_bytes > 0) {
		/* Do calculate how to fill this page.
		 * We will read PAGE_READ_BYTES bytes from FILE
		 * and zero the final PAGE_ZERO_BYTES bytes. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		/* Get a page of memory. */
		uint8_t *kpage = palloc_get_page (PAL_USER);
		if (kpage == NULL)
			return false;

		/* Load this page. */
		if (file_read (file, kpage, page_read_bytes) != (int) page_read_bytes) {
			palloc_free_page (kpage);
			return false;
		}
		memset (kpage + page_read_bytes, 0, page_zero_bytes);

		/* Add the page to the process's address space. */
		if (!install_page (upage, kpage, writable)) {
			printf("fail\n");
			palloc_free_page (kpage);
			return false;
		}

		/* Advance. */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		upage += PGSIZE;
	}
	return true;
}

/* Create a minimal stack by mapping a zeroed page at the USER_STACK */
bool
setup_stack (struct intr_frame *if_) {
	uint8_t *kpage;
	bool success = false;

	kpage = palloc_get_page (PAL_USER | PAL_ZERO);
	if (kpage != NULL) {
		success = install_page (((uint8_t *) USER_STACK) - PGSIZE, kpage, true);
		if (success)
			if_->rsp = USER_STACK;
		else
			palloc_free_page (kpage);
	}
	return success;
}

/* Adds a mapping from user virtual address UPAGE to kernel
 * virtual address KPAGE to the page table.
 * If WRITABLE is true, the user process may modify the page;
 * otherwise, it is read-only.
 * UPAGE must not already be mapped.
 * KPAGE should probably be a page obtained from the user pool
 * with palloc_get_page().
 * Returns true on success, false if UPAGE is already mapped or
 * if memory allocation fails. */
static bool
install_page (void *upage, void *kpage, bool writable) {
	struct thread *t = thread_current ();

	/* Verify that there's not already a page at that virtual
	 * address, then map our page there. */
	return (pml4_get_page (t->pml4, upage) == NULL
			&& pml4_set_page (t->pml4, upage, kpage, writable));
}
#else
/* From here, codes will be used after project 3.
 * If you want to implement the function for only project 2, implement it on the
 * upper block. */
/*
	lazy_load_segment: 실행 파일의 Page에 대한 이니셜라이저 함수

	Page fault가 발생할 때 호출됨, aux는 load_segment에서 설정한 정보
	vm_alloc_page_with_initializer의 네번째 인수로 이 함수가 들어감
*/
bool
lazy_load_segment (struct page *page, void *aux) {
	/* TODO: Load the segment from the file */
	/* TODO: This called when the first page fault occurs on address VA. */
	/* TODO: VA is available when calling this function. */

	struct container *container = (struct container *)aux;

	struct file *file = container->file;
	off_t offsetof = container->offset;
	size_t read_bytes = container->read_bytes;
    size_t zero_bytes = PGSIZE - read_bytes;

	file_seek(file, offsetof);

    if (file_read(file, page->frame->kva, read_bytes) != (int)read_bytes) {
		palloc_free_page(page->frame->kva);
        return false;
    }
    memset(page->frame->kva + read_bytes, 0, zero_bytes);

    return true;
}

/* Loads a segment starting at offset OFS in FILE at address
 * UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
 * memory are initialized, as follows:
 *
 * - READ_BYTES bytes at UPAGE must be read from FILE
 * starting at offset OFS.
 *
 * - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.
 *
 * The pages initialized by this function must be writable by the
 * user process if WRITABLE is true, read-only otherwise.
 *
 * Return true if successful, false if a memory allocation error
 * or disk read error occurs. */
/*
	Project 3: Lazy Load

	이전에는 프로세스가 실행될 때 Segment를 실제 메모리에 직접 로드하는 방식
	그래서 프로젝트2 까지 Page fault는 커널이나 유저 프로그램에서 나타나는 버그였는데,
	이제는 SPT를 통해 Page fault가 발생했을 때 (즉 페이지가 요청되었을 때) 메모리에 로드하는 방식으로 변경

	load_segment: file을 읽어들여 세그먼트 단위로 잘라 초기화하는 함수
	각 페이지들을 uninit 상태로 초기화하기 위해 vm_alloc_page_with_initializer() 호출
*/
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes, bool writable) {

	ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
	ASSERT (pg_ofs (upage) == 0);
	ASSERT (ofs % PGSIZE == 0);

	while (read_bytes > 0 || zero_bytes > 0) {
		/* Do calculate how to fill this page.
		 * We will read PAGE_READ_BYTES bytes from FILE
		 * and zero the final PAGE_ZERO_BYTES bytes. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		/* TODO: Set up aux to pass information to the lazy_load_segment. */
		struct container *container = (struct container *)malloc(sizeof(struct container));
		container->file = file;
		container->read_bytes = page_read_bytes;
		container->offset = ofs;

		if (!vm_alloc_page_with_initializer (VM_ANON, upage, writable, lazy_load_segment, container)) {
			return false;
		}

		/* Advance. */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		upage += PGSIZE;
		ofs += page_read_bytes;
	}

	return true;
}

/* Create a PAGE of stack at the USER_STACK. Return true on success. */
/*
	setup_stack: 스택 사이즈를 늘려주는 함수

	증가 시점: 할당해주지 않은 페이지에 rsp가 접근했을 때, 즉 Stack growth에 대한 page fault가 발생했을 때
	addr에 대한 새 페이지를 할당하고, 이에 맞춰 스택 마커도 표시 -> 페이지와 프레임을 연결하고 스레드의 stack_bottom 갱신
*/
bool
setup_stack (struct intr_frame *if_) {
	bool success = false;
	void *stack_bottom = (void *) (((uint8_t *) USER_STACK) - PGSIZE);

	/* TODO: Map the stack on stack_bottom and claim the page immediately.
	 * TODO: If success, set the rsp accordingly.
	 * TODO: You should mark the page is stack. */
	/* TODO: Your code goes here */

	struct thread *thread = thread_current();

	// vm_alloc_page: type, upage, writable
	if (vm_alloc_page(VM_ANON | VM_MARKER_0, stack_bottom, 1)) {
		success = vm_claim_page(stack_bottom);

		if (success) {
			if_->rsp = USER_STACK;
            thread->stack_bottom = stack_bottom;
		}
    }

	return success;
}
#endif /* VM */

/*
	push_arguments: argument들을 stack에 넣는 함수
	
	1) 문자열의 오른쪽부터 왼쪽 방향으로 넣어야 함, 스택은 아래로 확장하기 때문
	2) arg[4]는 NULL이므로 이를 제외하고(argc-1) 저장, arg[0] ~ arg[3]
	3) strlen()은 센티넬 빼고 셈, 센티넬을 포함해주어야 하기 때문에 (strlen + sizeof(""))
	4) 왜 rsp에 저장해요? rsp는 스택 포인터 레지스터로, user stack에서 현재 위치를 가리키는 역할
*/
void push_arguments(int argc, char **argv, struct intr_frame *if_) {
	// 1) argv[3][...] ~ argv[0][...]
	char *address[128];

	for (int i = argc-1; i >= 0; i--) {
		// 빼는 이유: 스택은 growing down이기 때문, 원하는 크기만큼 내려주고 빈 공간만큼 memcpy 해줌
		if_->rsp = if_->rsp - (strlen(argv[i]) + sizeof(""));
		memcpy(if_->rsp, argv[i], strlen(argv[i]) + sizeof(""));

		address[i] = if_->rsp;
	}

	// 2) Word-align: 8의 배수를 맞추기 위해 padding(0)을 넣는 것
	while (if_->rsp % 8 != 0) {
		if_->rsp -= 1;
		*(uint8_t *) if_->rsp = 0;
	}

	// 3) argv[4] ~ argv[0]
	for (int i = argc; i >= 0; i--) {
		if_->rsp = if_->rsp - 8;

		if (i == argc) {
			// argv[4]에는 0 넣음 (표 참고)
			memset(if_->rsp, 0, sizeof(char **));
		} else {
			// 나머지는 주소값을 넣음 (표 참고)
			memcpy(if_->rsp, &address[i], sizeof(char **));
		}
	}

	// 4) Return address
	if_->rsp = if_->rsp - 8;
	memset(if_->rsp, 0, sizeof(void *));

	/*
		rsi: 메모리를 이동하거나 비교할 때 출발 주소를 가르킴
		rdi: 메모리를 이동하거나 비교할 때 목적지 주소를 가르킴
		rsp: 스택에서 현재 위치를 가르킴, 스택의 삽입 및 삭제에 의해 변경됨

		rsi는 왜 rsp + 8을 하나요?
		: 위에서 4번 return address 값을 저장하기 위해서 - 8을 해주었음
		: rsi는 출발 주소여야 하기 때문에 address의 맨 앞을 가리켜야 하기 때문에 + 8 해주어야 함
		: 스택은 위에서 아래로 (높은 주소에서 낮은 주소로) growing한다는 것 주의
	*/
	if_->R.rdi  = argc;
	if_->R.rsi = if_->rsp + 8;
}

struct thread *find_child(int pid) {
    struct thread *curr = thread_current();
    struct list *child_list = &curr->child_list;

    struct list_elem *e;

	if (!list_empty(child_list)) {
		for (e = list_begin(child_list); e != list_end(child_list); e = list_next(e)){
			struct thread *t = list_entry(e, struct thread, child_elem);

			if (t->tid == pid){
				return t;
			}
		}
	}

    return NULL;
}