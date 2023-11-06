/*
 * mm-naive.c - The fastest, least memory-efficient malloc package.
 * 
 * In this naive approach, a block is allocated by simply incrementing
 * the brk pointer.  A block is pure payload. There are no headers or
 * footers.  Blocks are never coalesced or reused. Realloc is
 * implemented directly using mm_malloc and mm_free.
 *
 * NOTE TO STUDENTS: Replace this header comment with your own header
 * comment that gives a high level description of your solution.
 * 
 * Seglist
 * Perf index = 45 (util) + 40 (thru) = 85/100
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include "mm.h"
#include "memlib.h"

/*********************************************************
 * NOTE TO STUDENTS: Before you do anything else, please
 * provide your team information in the following struct.
 ********************************************************/
team_t team = {
    /* Team name */
    "Team 1",
    /* First member's full name */
    "Seo Jiwon",
    /* First member's email address */
    "moonlight_youlike@naver.com",
    /* Second member's full name (leave blank if none) */
    "",
    /* Second member's email address (leave blank if none) */
    ""
};

// 바이트 단위 워드 사이즈, 더블 워드 사이즈
#define WSIZE 4
#define DSIZE 8

// 힙의 사이즈를 2의 12승만큼 늘림
#define CHUNKSIZE (1<<12)
#define INITCHUNKSIZE (1<<6)

// 가용 연결 리스트의 개수 (최대값)
#define LISTLIMIT 20

#define MAX(x,y) ((x)>(y) ? (x) : (y))

// size: 블록 사이즈, alloc: 가용 여부 => 둘이 합치면 온전한 주소
#define PACK(size,alloc) ((size)|(alloc))

// P 주소값을 찾아가서 해당 값을 Read, Write
// P가 void *형이기 때문에 unsigned int *로 형변환
#define GET(p) (*(unsigned int *)(p))
#define PUT(p,val) (*(unsigned int *)(p)=(val))

// 마지막 비트 3개 제외한 나머지 값 => 블록 사이즈
#define GET_SIZE(p) (GET(p) & ~0X7)
// 마지막 비트 1개 값 => 가용 여부
#define GET_ALLOC(p) (GET(p) & 0X1)

// WSIZE를 빼는 이유: bp는 항상 payload의 시작점, bp에서 1워드만큼 앞으로 이동
#define HDRP(bp) ((char*)(bp) - WSIZE)
// DSIZE를 빼는 이유: 다음 블록의 H와 현재 블록의 F를 빼기 위해
#define FTRP(bp) ((char*)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)

#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE)))
#define PREV_BLKP(bp) ((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE)))

// P가 가르키는 곳을 PTR로 변경
#define SET_PTR(p, ptr) (*(unsigned int *)(p) = (unsigned int)(ptr))

// 가용 블럭 리스트에서 이전, 이후 블록 포인터를 반환
#define NEXT_PTR(ptr) ((char *)(ptr))
#define PREV_PTR(ptr) ((char *)(ptr) + WSIZE)

// 분리되어 있는 가용 리스트 내에서 이전, 이후 블록 포인터를 반환
#define NEXT(ptr) (*(char **)(ptr))
#define PREV(ptr) (*(char **)(PREV_PTR(ptr)))

// Jiwon Parameter & Function
char *heap_listp = 0;
void *free_list[LISTLIMIT];

static void *coalesce(void *bp);
static void *extend_heap(size_t words);
static void *find_fit(size_t asize);
static void place(void *bp, size_t asize);
static void insert_block(void *ptr, size_t size);
static void delete_block(void *ptr);

/* 
 * mm_init - initialize the malloc package.
 */
int mm_init(void) {
    // 전체 free list 초기화 단계
    for (int class = 0; class < LISTLIMIT; class++) {
        free_list[class] = NULL;
    }
    
    if ((heap_listp = mem_sbrk(4*WSIZE)) == (void *)-1) {
        return -1;
    }
    
    // 패딩(0), 프롤로그 H/F(1), 에필로그 H 생성(0)
    PUT(heap_listp, 0);
    PUT(heap_listp + (1*WSIZE), PACK(DSIZE, 1));
    PUT(heap_listp + (2*WSIZE), PACK(DSIZE, 1));
    PUT(heap_listp + (3*WSIZE), PACK(0, 1));
    
    // 프롤로그 F로 위치, 앞이나 뒤 블록으로 가기 위해
    heap_listp += (2*WSIZE);

    // extend_heap 함수는 워드 단위임
    if (extend_heap(INITCHUNKSIZE) == NULL) {
        return -1;
    }

    return 0;
}

/* 
 * mm_malloc - Allocate a block by incrementing the brk pointer.
 *     Always allocate a block whose size is a multiple of the alignment.
 */
void *mm_malloc(size_t size) {
    // asize: 블록 사이즈 조정
    // extendsize: 적당한 곳이 없으면 확장해야 함, 확장할 사이즈 저장
    size_t asize;
    size_t extendsize;
    char *bp;

    if (size == 0) {
        return NULL;
    }

    // 오버헤드, 정렬 사항 생각해서 블록 사이즈를 조정
    if (size <= DSIZE) {
        // H, F 포함하여 블록 사이즈를 조정해야 함으로
        asize = DSIZE * 2;
    } else {
        // DSIZE보다 클 때는 블록이 가질 수 있는 크기 중 최적화된 크기로 재조정
        // (DSIZE-1)는 8의 배수로 만들어주기 위한 코드, int 연산은 소수점 버림
        asize = DSIZE * ((size + (DSIZE) + (DSIZE-1)) / DSIZE);
    }
    
    // 가용 블록 검색 후 요청한 블록 배치 (검색 -> 배치)
    if ((bp = find_fit(asize)) != NULL) {
        place(bp, asize);
        return bp;
    }

    // 알맞은 가용 블록이 없을 경우 확장 후 블록 배치 (검색 -> 확장 -> 배치)
    extendsize = MAX(asize, CHUNKSIZE);
    if ((bp = extend_heap(extendsize/WSIZE)) == NULL) {
        return NULL;
    }

    place(bp, asize);

    return bp;
}

/*
 * mm_free - Freeing a block does nothing.
 */
// 블록을 반환하고 인접 가용 블록들과 통합 (통합은 coalesce에서 수행)
void mm_free(void *ptr) {
    size_t size = GET_SIZE(HDRP(ptr));

    // H, F를 0으로 할당
    PUT(HDRP(ptr), PACK(size, 0));
    PUT(FTRP(ptr), PACK(size, 0));

    coalesce(ptr);
}

/*
 * mm_realloc - Implemented simply in terms of mm_malloc and mm_free
 */
void *mm_realloc(void *ptr, size_t size) {
    void *oldptr = ptr;
    void *newptr;
    size_t copySize;
    
    newptr = mm_malloc(size);
    if (newptr == NULL) {
        return NULL;
    }

    copySize = GET_SIZE(HDRP(oldptr));

    // 기존 사이즈가 요청 사이즈보다 크면, 기존 사이즈 갱신
    if (size < copySize) {
        copySize = size;
    }

    // memcpy 함수: 메모리의 특정 부분을 다른 메모리 영역으로 복사
    // oldptr로부터 copySize만큼의 문자를 newptr로 복사하렴
    memcpy(newptr, oldptr, copySize);
    mm_free(oldptr);

    return newptr;
}

// Jiwon Parameter & Function

// 1) 힙이 초기화될 때: 초기화 후 초기 가용 블록을 생성하기 위해 호출
// 2) 요청한 크기를 할당할만한 충분한 공간을 찾지 못했을 때: 추가 힙 공간을 요청
static void *extend_heap(size_t words) {
	char *bp;
	size_t size;

    // 2워드의 배수로 만들어 byte 단위로 만들어줌
	size = (words % 2) ? (words+1) * WSIZE : words * WSIZE;
	if ((long)(bp = mem_sbrk(size)) == -1) {
	    return NULL;
    }

    // 새로운 가용 블록의 H, F 생성
	PUT(HDRP(bp), PACK(size, 0));
	PUT(FTRP(bp), PACK(size, 0));

    // 새로 가용 블록을 만들었으니 에필로그를 새롭게 위치 시켜야 함
    // 에필로그는 새로 만든 블록의 다음 블록
	PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1));

	return coalesce(bp);
}

// 할당 블록을 가용 블록으로 변환할 때,
// 해당 블록의 인접 블록이 가용 블록인지 확인해야 한다
static void *coalesce(void *bp) {
    size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp)));
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    size_t size = GET_SIZE(HDRP(bp));

    // 2) 현재 블록 + 다음 블록
    if (prev_alloc && !next_alloc) {
        delete_block(NEXT_BLKP(bp));

        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));

    // 3) 이전 블록 + 현재 블록
    } else if (!prev_alloc && next_alloc) {
        delete_block(PREV_BLKP(bp));

        size += GET_SIZE(HDRP(PREV_BLKP(bp)));
        PUT(FTRP(bp), PACK(size, 0));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));

        // 이전 블록의 payload를 보도록 함
        bp = PREV_BLKP(bp);

    // 4) 이전 + 현재 + 다음
    } else if (!prev_alloc && !next_alloc) {
        delete_block(PREV_BLKP(bp));
        delete_block(NEXT_BLKP(bp));

        size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(FTRP(NEXT_BLKP(bp)));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0));

        // 이전 블록의 payload를 보도록 함
        bp = PREV_BLKP(bp);
    }
    
    insert_block(bp, size);

    return bp;
}

static void *find_fit(size_t asize) {
    char *bp;

    int class = 0;
    size_t target = asize;

    // 분리 가용 리스트에서 가용 리스트 검색
    while (class < LISTLIMIT) {
        // 1) 마지막 클래스거나,
        // 2) target이 1보다 작거나 같고 가용 리스트 값이 널이 아닐 때,
        if ((class == LISTLIMIT - 1) || ((target <= 1) && (free_list[class] != NULL))) {
            bp = free_list[class];

            // 가용 상태이고, 요청한 사이즈만큼 충분한 공간이 있을 때
            while ((bp != NULL) && ((asize > GET_SIZE(HDRP(bp))))) {
                bp = NEXT(bp);
            }

            if (bp != NULL) {
                return bp;
            }
        }
        
        target >>= 1;

        class++;
    }

    return NULL;
}

// 맞는 블록이 있으면 배치하고, 남으면 가용 블록으로 분할
static void place(void *bp, size_t asize){
    // asize: 요청한 블록 사이즈
    // csize: 가용 블록 사이즈
    size_t csize = GET_SIZE(HDRP(bp));

    delete_block(bp);

    // 1) 초과 됐을 때: 요청 블록에 넣고 남은 사이즈는 가용 블록으로 분할
    if ((csize - asize) >= (2 * DSIZE)) {
        PUT(HDRP(bp), PACK(asize, 1));
        PUT(FTRP(bp), PACK(asize, 1));

        bp = NEXT_BLKP(bp);
        PUT(HDRP(bp), PACK(csize - asize, 0));
        PUT(FTRP(bp), PACK(csize - asize, 0));

        insert_block(bp,(csize-asize));

    // 2) 분할하지 않아도 될 때: 남은 블록이 2*DSIZE보다 작으면 데이터를 담을 수 없음
    } else {
        PUT(HDRP(bp), PACK(csize, 1));
        PUT(FTRP(bp), PACK(csize, 1));
    }
}

static void insert_block(void *ptr, size_t size) {
    int class = 0;

    // search_ptr: 노드 자리를 검색해나감, insert_ptr: 실제 노드가 삽입되는 위치
    void *search_ptr = ptr; 
    void *insert_ptr = NULL; 
    
    // 분리 가용 리스트 중에서 적절한 것을 선택함
    while ((class < LISTLIMIT - 1) && (size > 1)) {
        size >>= 1;
        class++;
    }
    
    // 크기가 작은 것부터 검색함
    // 분리 가용 리스트의 시작 위치를 search_ptr에 저장
    search_ptr = free_list[class];
    while ((search_ptr != NULL) && (size > GET_SIZE(HDRP(search_ptr)))) {
        insert_ptr = search_ptr;
        search_ptr = NEXT(search_ptr);
    }
    
    if (search_ptr != NULL) {
        // 1) 분리 가용 리스트의 가운데로 삽입할 때,
        if (insert_ptr != NULL) {
            SET_PTR(NEXT_PTR(ptr), search_ptr);
            SET_PTR(PREV_PTR(search_ptr), ptr);
            SET_PTR(PREV_PTR(ptr), insert_ptr);
            SET_PTR(NEXT_PTR(insert_ptr), ptr);

        // 2) 분리 가용 리스트의 맨 처음에 삽입할 때,
        } else {
            SET_PTR(NEXT_PTR(ptr), search_ptr);
            SET_PTR(PREV_PTR(search_ptr), ptr);
            SET_PTR(PREV_PTR(ptr), NULL);
            
            // 분리 가용 리스트의 시작 위치를 search_ptr에 저장
            free_list[class] = ptr;
        }

    } else {
        // 3) 분리 가용 리스트의 맨 마지막에 삽입할 때,
        if (insert_ptr != NULL) {
            SET_PTR(NEXT_PTR(ptr), NULL);
            SET_PTR(PREV_PTR(ptr), insert_ptr);
            SET_PTR(NEXT_PTR(insert_ptr), ptr);

        // 4) 분리 가용 리스트에 원소가 없을 때,
        } else {
            SET_PTR(NEXT_PTR(ptr), NULL);
            SET_PTR(PREV_PTR(ptr), NULL);
            
            // 분리 가용 리스트의 시작 위치를 search_ptr에 저장
            free_list[class] = ptr;
        }
    }
    
    return;
}

static void delete_block(void *ptr) {
    size_t size = GET_SIZE(HDRP(ptr));

    int list = 0;
    
    // 분리 가용 리스트 중에서 적절한 것을 선택함
    while ((list < LISTLIMIT - 1) && (size > 1)) {
        size >>= 1;
        list++;
    }
    
    if (NEXT(ptr) != NULL) { 
        // 1) 분리 가용 리스트의 가운데로 삽입할 때,
        if (PREV(ptr) != NULL) {
            SET_PTR(PREV_PTR(NEXT(ptr)), PREV(ptr));
            SET_PTR(NEXT_PTR(PREV(ptr)), NEXT(ptr));

        // 2) 분리 가용 리스트의 맨 처음에 삽입할 때,
        } else {
            SET_PTR(PREV_PTR(NEXT(ptr)), NULL);

            // 분리 가용 리스트의 시작 위치를 다음 블록으로 갱신
            free_list[list] = NEXT(ptr);
        }
    } else {
        // 3) 분리 가용 리스트의 맨 마지막에 삽입할 때,
        if (PREV(ptr) != NULL) {
            SET_PTR(NEXT_PTR(PREV(ptr)), NULL);

        // 4) 분리 가용 리스트의 마지막 원소일 때,
        } else {

            // 분리 가용 리스트의 시작 위치를 Null로 갱신
            free_list[list] = NULL;
        }
    }
    
    return;
}