#include "threads/init.h"
#include <console.h>
#include <debug.h>
#include <limits.h>
#include <random.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "devices/kbd.h"
#include "devices/input.h"
#include "devices/serial.h"
#include "devices/timer.h"
#include "devices/vga.h"
#include "threads/interrupt.h"
#include "threads/io.h"
#include "threads/loader.h"
#include "threads/malloc.h"
#include "threads/mmu.h"
#include "threads/palloc.h"
#include "threads/pte.h"
#include "threads/thread.h"
#ifdef USERPROG
#include "userprog/process.h"
#include "userprog/exception.h"
#include "userprog/gdt.h"
#include "userprog/syscall.h"
#include "userprog/tss.h"
#endif
#include "tests/threads/tests.h"
#ifdef VM
#include "vm/vm.h"
#endif
#ifdef FILESYS
#include "devices/disk.h"
#include "filesys/filesys.h"
#include "filesys/fsutil.h"
#endif

/* Page-map-level-4 with kernel mappings only. */
uint64_t *base_pml4;

#ifdef FILESYS
/* -f: Format the file system? */
static bool format_filesys;
#endif

/* -q: Power off after kernel tasks complete? */
bool power_off_when_done;

bool thread_tests;

static void bss_init (void);
static void paging_init (uint64_t mem_end);

static char **read_command_line (void);
static char **parse_options (char **argv);
static void run_actions (char **argv);
static void usage (void);

static void print_stats (void);


int main (void) NO_RETURN;

/* Pintos main program. */
// main: 메모리, 스레드, Page Table 등을 초기화하는 함수
int
main (void) {
	uint64_t mem_end;
	char **argv;

	/* Clear BSS and get machine's RAM size. */
	/*
		bss (block started by symbol)
		: 초기화되지 않은 (혹은 초기화를 0이나 NULL로 한) 정적 및 전역 데이터를 위한 영역

		왜 초기화되지 않은 데이터를 위한 공간이 따로 있나요?
		: 전체 프로그램의 크기를 작게 만들 수 있기 때문
		: 초기값이 주어진 data 영역에 들어가는 변수들은 변수마다 값을 넣어주는 공간만큼 용량을 차지함
		: 초기화되지 않은 bss에 들어가는 변수들은 그 값을 넣어줄 필요가 없기에, 변수만 알려주면 됨
		: 따라서 변수의 값을 넣을 필요가 없어 그 만큼 프로그램의 용량이 작아지게 됨
	*/
	bss_init ();

	/* Break command line into arguments and parse options. */
	/*
		read_command_line: 명령어 읽어들임
		parse_options: 해당 라인을 Parsing하여 argv에 담아 넘김
	*/
	argv = read_command_line ();
	argv = parse_options (argv);

	/* Initialize ourselves as a thread so we can use locks,
	   then enable console locking. */
	/*
		thread_init
		: 해당 함수 안의 init_thread()가 initial 스레드인 main 스레드를 생성함 (main 스레드는 커널 스레드)
		: tid는 1로 생성됨

		console_init
		: console_lock을 초기화, vprintf, putbuf 등 출력에 사용되는 lock
	*/
	thread_init ();
	console_init ();

	/* Initialize memory system. */
	/*
		메모리 초기화
		palloc_init: page allocator를 초기화, 사용 가능한 메모리 사이즈를 얻음
		malloc_init: malloc descriptor를 초기화
		paging_init: palloc_get_page() 통해 다음 cpu가 새 페이지 디렉토리를 사용하도록 함
	*/
	mem_end = palloc_init ();
	malloc_init ();
	paging_init (mem_end);

#ifdef USERPROG
	/*
		tss (task state segment)
		: 해당 함수의 tss_update()를 보면, tss의 rsp0가 커널 스택의 끝을 가리키도록 함
		: 사용자 프로세스가 인터럽트 핸들러에 들어갈 때 하드웨어가 커널의 스택 포인터를 찾기 위해 tss를 참조함

		gdt (global desriptor table)
		: 프로그램 실행 중에 사용되는 다양한 메모리 영역의 특성을 정의하기 위해 인텔 프로세스에서 사용하는 데이터 구조
		: 기본 주소, 크기, 실행 가능성 및 쓰기 가능성과 같은 액세스 권한을 포함하고 있음
		: gdt_init()은 사용자 영역에 있는 gdt들을 초기화 시킴
	*/
	tss_init ();
	gdt_init ();
#endif

	/* Initialize interrupt handlers. */
	intr_init ();
	timer_init ();
	kbd_init ();
	input_init ();
#ifdef USERPROG
	exception_init ();
	syscall_init ();
#endif
	/* Start thread scheduler and enable interrupts. */
	/*
		serial_init_queue: 외부 디바이스를 쓰기 위한 초기화
		timer_calibrate: 타이머가 자동으로 작동되도록
	*/
	thread_start ();
	serial_init_queue ();
	timer_calibrate ();

#ifdef FILESYS
	/* Initialize file system. */
	disk_init ();
	filesys_init (format_filesys);
#endif

#ifdef VM
	vm_init ();
#endif

	printf ("Boot complete.\n");

	/* Run actions specified on kernel command line. */
	/*
		run_actions: action 구조체 정의, 이 안에서 run_task 함수 실행
		run_task: action 구조체를 탐색하면서 argv와 action name을 비교하여 미리 정의된 function 실행
	*/
	run_actions (argv);

	/* Finish up. */
	if (power_off_when_done)
		power_off ();
	thread_exit ();
}

/* Clear BSS */
static void
bss_init (void) {
	/* The "BSS" is a segment that should be initialized to zeros.
	   It isn't actually stored on disk or zeroed by the kernel
	   loader, so we have to zero it ourselves.

	   The start and end of the BSS segment is recorded by the
	   linker as _start_bss and _end_bss.  See kernel.lds. */
	extern char _start_bss, _end_bss;
	memset (&_start_bss, 0, &_end_bss - &_start_bss);
}

/*
	63          48 47            39 38            30 29            21 20         12 11         0
	+-------------+----------------+----------------+----------------+-------------+------------+
	| Sign Extend |    Page-Map    | Page-Directory | Page-directory |  Page-Table |  Physical  |
	|             | Level-4 Offset |    Pointer     |     Offset     |   Offset    |   Offset   |
	+-------------+----------------+----------------+----------------+-------------+------------+
				|                |                |                |             |            |
				+------- 9 ------+------- 9 ------+------- 9 ------+----- 9 -----+---- 12 ----+
											Virtual Address

	초기화해주어야 할 곳
	1) PML4 (page-map level 4)
	2) PDP (page-directory pointer)
	3) PD (page-directory)
	4) PT (page-table)
*/

/* Populates the page table with the kernel virtual mapping,
 * and then sets up the CPU to use the new page directory.
 * Points base_pml4 to the pml4 it creates. */
static void
paging_init (uint64_t mem_end) {
	uint64_t *pml4, *pte;
	int perm;
	pml4 = base_pml4 = palloc_get_page (PAL_ASSERT | PAL_ZERO);

	extern char start, _end_kernel_text;
	// Maps physical address [0 ~ mem_end] to
	// [LOADER_KERN_BASE ~ LOADER_KERN_BASE + mem_end].
	for (uint64_t pa = 0; pa < mem_end; pa += PGSIZE) {
		uint64_t va = (uint64_t) ptov(pa);

		perm = PTE_P | PTE_W;
		if ((uint64_t) &start <= va && va < (uint64_t) &_end_kernel_text)
			perm &= ~PTE_W;

		if ((pte = pml4e_walk (pml4, va, 1)) != NULL)
			*pte = pa | perm;
	}

	// reload cr3
	pml4_activate(0);
}

/* Breaks the kernel command line into words and returns them as
   an argv-like array. */
static char **
read_command_line (void) {
	static char *argv[LOADER_ARGS_LEN / 2 + 1];
	char *p, *end;
	int argc;
	int i;

	argc = *(uint32_t *) ptov (LOADER_ARG_CNT);
	p = ptov (LOADER_ARGS);
	end = p + LOADER_ARGS_LEN;
	for (i = 0; i < argc; i++) {
		if (p >= end)
			PANIC ("command line arguments overflow");

		argv[i] = p;
		p += strnlen (p, end - p) + 1;
	}
	argv[argc] = NULL;

	/* Print kernel command line. */
	printf ("Kernel command line:");
	for (i = 0; i < argc; i++)
		if (strchr (argv[i], ' ') == NULL)
			printf (" %s", argv[i]);
		else
			printf (" '%s'", argv[i]);
	printf ("\n");

	return argv;
}

/* Parses options in ARGV[]
   and returns the first non-option argument. */
static char **
parse_options (char **argv) {
	for (; *argv != NULL && **argv == '-'; argv++) {
		char *save_ptr;
		char *name = strtok_r (*argv, "=", &save_ptr);
		char *value = strtok_r (NULL, "", &save_ptr);

		if (!strcmp (name, "-h"))
			usage ();
		else if (!strcmp (name, "-q"))
			power_off_when_done = true;
#ifdef FILESYS
		else if (!strcmp (name, "-f"))
			format_filesys = true;
#endif
		else if (!strcmp (name, "-rs"))
			random_init (atoi (value));
		else if (!strcmp (name, "-mlfqs"))
			thread_mlfqs = true;
#ifdef USERPROG
		else if (!strcmp (name, "-ul"))
			user_page_limit = atoi (value);
		else if (!strcmp (name, "-threads-tests"))
			thread_tests = true;
#endif
		else
			PANIC ("unknown option `%s' (use -h for help)", name);
	}

	return argv;
}

/* Runs the task specified in ARGV[1]. */
static void
run_task (char **argv) {
	const char *task = argv[1];

	printf ("Executing '%s':\n", task);
#ifdef USERPROG
	if (thread_tests){
		run_test (task);
	} else {
		/*
			실행 순서: process_create_initd() -> process_wait()

			process_create_initd: 사용자 프로그램을 실행시킴
			process_wait: process_create_initd()에서 리턴된 tid가 process_wait()의 인수로 가는 것, 즉 이 tid가 waiting하는 것
		*/
		process_wait (process_create_initd (task));
	}
#else
	run_test (task);
#endif
	printf ("Execution of '%s' complete.\n", task);
}

/* Executes all of the actions specified in ARGV[]
   up to the null pointer sentinel. */
// run_actions: 명령어에 맞는 동작을 실행하는 함수, 현재는 "run" action만 확인하면 됨
static void
run_actions (char **argv) {
	/* An action. */
	struct action {
		char *name;                       /* Action name. */
		int argc;                         /* # of args, including action name. */
		void (*function) (char **argv);   /* Function to execute action. */
	};

	/* Table of supported actions. */
	static const struct action actions[] = {
		{"run", 2, run_task},
#ifdef FILESYS
		{"ls", 1, fsutil_ls},
		{"cat", 2, fsutil_cat},
		{"rm", 2, fsutil_rm},
		{"put", 2, fsutil_put},
		{"get", 2, fsutil_get},
#endif
		{NULL, 0, NULL},
	};

	while (*argv != NULL) {
		const struct action *a;
		int i;

		/* Find action name. */
		for (a = actions; ; a++)
			if (a->name == NULL)
				PANIC ("unknown action `%s' (use -h for help)", *argv);
			else if (!strcmp (*argv, a->name))
				break;

		/* Check for required arguments. */
		for (i = 1; i < a->argc; i++)
			if (argv[i] == NULL)
				PANIC ("action `%s' requires %d argument(s)", *argv, a->argc - 1);

		/* Invoke action and advance. */
		a->function (argv);
		argv += a->argc;
	}

}

/* Prints a kernel command line help message and powers off the
   machine. */
static void
usage (void) {
	printf ("\nCommand line syntax: [OPTION...] [ACTION...]\n"
			"Options must precede actions.\n"
			"Actions are executed in the order specified.\n"
			"\nAvailable actions:\n"
#ifdef USERPROG
			"  run 'PROG [ARG...]' Run PROG and wait for it to complete.\n"
#else
			"  run TEST           Run TEST.\n"
#endif
#ifdef FILESYS
			"  ls                 List files in the root directory.\n"
			"  cat FILE           Print FILE to the console.\n"
			"  rm FILE            Delete FILE.\n"
			"Use these actions indirectly via `pintos' -g and -p options:\n"
			"  put FILE           Put FILE into file system from scratch disk.\n"
			"  get FILE           Get FILE from file system into scratch disk.\n"
#endif
			"\nOptions:\n"
			"  -h                 Print this help message and power off.\n"
			"  -q                 Power off VM after actions or on panic.\n"
			"  -f                 Format file system disk during startup.\n"
			"  -rs=SEED           Set random number seed to SEED.\n"
			"  -mlfqs             Use multi-level feedback queue scheduler.\n"
#ifdef USERPROG
			"  -ul=COUNT          Limit user memory to COUNT pages.\n"
#endif
			);
	power_off ();
}


/* Powers down the machine we're running on,
   as long as we're running on Bochs or QEMU. */
void
power_off (void) {
#ifdef FILESYS
	filesys_done ();
#endif

	print_stats ();

	printf ("Powering off...\n");
	outw (0x604, 0x2000);               /* Poweroff command for qemu */
	for (;;);
}

/* Print statistics about Pintos execution. */
static void
print_stats (void) {
	timer_print_stats ();
	thread_print_stats ();
#ifdef FILESYS
	disk_print_stats ();
#endif
	console_print_stats ();
	kbd_print_stats ();
#ifdef USERPROG
	exception_print_stats ();
#endif
}
