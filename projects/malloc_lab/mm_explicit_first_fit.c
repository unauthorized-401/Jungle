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
 * Explicit + First-fit
 * Perf index = 42 (util) + 40 (thru) = 82/100
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

#define MAX(x, y) ((x) > (y)? (x) : (y))

// size: 블록 사이즈, alloc: 가용 여부 => 둘이 합치면 온전한 주소
#define PACK(size, alloc) ((size) | (alloc))

// P 주소값을 찾아가서 해당 값을 Read, Write
// P가 void *형이기 때문에 unsigned int *로 형변환
#define GET(p) (*(unsigned int *)(p))
#define PUT(p, val) (*(unsigned int *)(p) = (val))

// 마지막 비트 3개 제외한 나머지 값 => 블록 사이즈
#define GET_SIZE(p) (GET(p) & ~0x7)
// 마지막 비트 1개 값 => 가용 여부
#define GET_ALLOC(p) (GET(p) & 0x1)

// WSIZE를 빼는 이유: bp는 항상 payload의 시작점, bp에서 1워드만큼 앞으로 이동
#define HDRP(bp) ((char *)(bp) - WSIZE)
// DSIZE를 빼는 이유: 다음 블록의 H와 현재 블록의 F를 빼기 위해
#define FTRP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)

#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE)))
#define PREV_BLKP(bp) ((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE)))

// 이전 가용 블록, 이후 가용 블록을 찾기 위해
#define PRED_P(bp)  (*(void **)(bp))
#define SUCC_P(bp)  (*(void **)((bp) + WSIZE))

// Jiwon Parameter & Function
static void *heap_listp;
static void *extend_heap(size_t words);
static void *coalesce(void *bp);
static void *find_fit(size_t asize);
static void place(void *p, size_t size);

static void list_add(void *p);
static void list_remove(void *p);

/* 
 * mm_init - initialize the malloc package.
 */
int mm_init(void) {
    if ((heap_listp = mem_sbrk(6*WSIZE)) == (void *)-1) {
        return -1;
    }

    PUT(heap_listp, 0);
    PUT(heap_listp + (1*WSIZE), PACK(2*DSIZE, 1));
    PUT(heap_listp + (2*WSIZE), heap_listp + (3*WSIZE));
    PUT(heap_listp + (3*WSIZE), heap_listp + (2*WSIZE));
    PUT(heap_listp + (4*WSIZE), PACK(2*DSIZE, 1));
    PUT(heap_listp + (5*WSIZE), PACK(0, 1));

    // 프롤로그 F로 위치, 앞이나 뒤 블록으로 가기 위해
    heap_listp += (2*WSIZE);

    // extend_heap 함수는 워드 단위임
    if (extend_heap(CHUNKSIZE/WSIZE) == NULL) {
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
static void *coalesce(void *bp){
    size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp)));
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    size_t size = GET_SIZE(HDRP(bp));

    // 2) 현재 블록 + 다음 블록
    if (prev_alloc && !next_alloc) {
        list_remove(NEXT_BLKP(bp));

        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));

    // 3) 이전 블록 + 현재 블록
    } else if (!prev_alloc && next_alloc) {
        list_remove(PREV_BLKP(bp));

        size += GET_SIZE(HDRP(PREV_BLKP(bp)));
        PUT(FTRP(bp), PACK(size, 0));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);

    // 4) 이전 + 현재 + 다음
    } else if (!prev_alloc && !next_alloc) {
        list_remove(PREV_BLKP(bp));
        list_remove(NEXT_BLKP(bp));

        size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(FTRP(NEXT_BLKP(bp)));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
    }

    list_add(bp);
    
    return bp;
}

// 가용 블록 검색: First-Fit 방법 사용
static void *find_fit(size_t asize){
    void *bp;

    for (bp = SUCC_P(heap_listp); !GET_ALLOC(HDRP(bp)) ; bp = SUCC_P(bp)){
        if (asize <= GET_SIZE(HDRP(bp))){
            return bp;
        }
    }

    return NULL;
}

// 맞는 블록이 있으면 배치하고, 남으면 가용 블록으로 분할
static void place(void *p, size_t size) {
    size_t free_block = GET_SIZE(HDRP(p));
    list_remove(p);

    // 1) 초과 됐을 때: 요청 블록에 넣고 남은 사이즈는 가용 블록으로 분할
    if ((free_block - size) >= (2 * DSIZE)) {
        PUT(HDRP(p), PACK(size, 1));
        PUT(FTRP(p), PACK(size, 1));

        p = NEXT_BLKP(p);
        PUT(HDRP(p), PACK(free_block-size, 0));
        PUT(FTRP(p), PACK(free_block-size, 0));

        list_add(p);
        
    // 2) 분할하지 않아도 될 때: 남은 블록이 2*DSIZE보다 작으면 데이터를 담을 수 없음
    } else {
        PUT(HDRP(p), PACK(free_block, 1));
        PUT(FTRP(p), PACK(free_block, 1));
    }
}

// 가용 연결 리스트에 추가
static void list_add(void *p) {
    // p의 PRED와 SUCC 값 설정
    PRED_P(p) = heap_listp;
    SUCC_P(p) = SUCC_P(heap_listp);
    
    // 위에서 설정한 p를, 가용 리스트에 추가 
    PRED_P(SUCC_P(heap_listp)) = p;
    SUCC_P(heap_listp) = p;
}

// 가용 연결 리스트에서 삭제
static void list_remove(void *p) {
    // 연결을 끊어야 함으로, p의 앞 뒤 블럭을 연결
    PRED_P(SUCC_P(p)) = PRED_P(p);
    SUCC_P(PRED_P(p)) = SUCC_P(p);
}