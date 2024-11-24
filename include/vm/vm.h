#ifndef VM_VM_H
#define VM_VM_H
#include <stdbool.h>
#include "threads/palloc.h"
#include "lib/kernel/hash.h"
#include "devices/disk.h"
#include "hash.h"

enum vm_type {
    /* page not initialized */
    VM_UNINIT = 0,
    /* page not related to the file, aka anonymous page */
    VM_ANON = 1,
    /* page that realated to the file */
    VM_FILE = 2,
    /* page that hold the page cache, for project 4 */
    VM_PAGE_CACHE = 3,

    /* Bit flags to store state */

    /* Auxillary bit flag marker for store information. You can add more
     * markers, until the value is fit in the int. */
    VM_MARKER_0 = (1 << 3),
    VM_MARKER_1 = (1 << 4),

    /* DO NOT EXCEED THIS VALUE. */
    VM_MARKER_END = (1 << 31),
};

#include "vm/uninit.h"
#include "vm/anon.h"
#include "vm/file.h"
#ifdef EFILESYS
#include "filesys/page_cache.h"
#endif

struct page_operations;
struct thread;

#define VM_TYPE(type) ((type) & 7)

/* The representation of "page".
 * This is kind of "parent class", which has four "child class"es, which are
 * uninit_page, file_page, anon_page, and page cache (project4).
 * DO NOT REMOVE/MODIFY PREDEFINED MEMBER OF THIS STRUCTURE. */
struct page {//가상 메모리 페이지의 기본 구조체
    const struct page_operations *operations;//스왑인아웃,삭제 처리
    void *va;              /* Address in terms of user space */
    struct frame *frame;   /* Back reference for frame */


    /* 하혜민 */
    struct hash_elem hash_elem;
    bool writable;
    int mapped_page_count; // file_backed_page인 경우, 매핑에 사용한 페이지 개수 (매핑 해제 시 사용)

    /* Per-type data are binded into the union.
     * Each function automatically detects the current union */
    union {//실제 데이터는 유니온으로 각 페이지 타입에 따라 다르게 저장됨
        struct uninit_page uninit; //비초기화 페이지
        struct anon_page anon; //익명 페이지
        struct file_page file; //파일 페이지
#ifdef EFILESYS
        struct page_cache page_cache;
#endif
    };
};

/* The representation of "frame" */
struct frame {//물리적 메모리에서의 페이지 위치
    void *kva; //물리 메모리 주소
    struct page *page; //이 프레임에 대응하는 가상 메모리 페이지의 page 포인터
    struct list_elem frame_elem;
    
};

/* The function table for page operations.
 * This is one way of implementing "interface" in C.
 * Put the table of "method" into the struct's member, and
 * call it whenever you needed. */
struct page_operations {
    bool (*swap_in) (struct page *, void *);
    bool (*swap_out) (struct page *);
    void (*destroy) (struct page *);
    enum vm_type type;
};

#define swap_in(page, v) (page)->operations->swap_in ((page), v)
#define swap_out(page) (page)->operations->swap_out (page)
#define destroy(page) \
    if ((page)->operations->destroy) (page)->operations->destroy (page)

/* Representation of current process's memory space.
 * We don't want to force you to obey any specific design for this struct.
 * All designs up to you for this. */
struct supplemental_page_table {
    //hash table 사용. 하혜민
    struct hash spt_hash;
};


// /* Hash table. */
// struct hash {
//  size_t elem_cnt;            /* Number of elements in table. */
//  size_t bucket_cnt;          /* Number of buckets, a power of 2. */
//  struct list *buckets;       /* Array of `bucket_cnt' lists. */
//  hash_hash_func *hash;       /* Hash function. */
//  hash_less_func *less;       /* Comparison function. */
//  void *aux;                  /* Auxiliary data for `hash' and `less'. */
// };

// /* Hash element. */
// struct hash_elem {
//  struct list_elem list_elem;
// };

#include "threads/thread.h"
void supplemental_page_table_init (struct supplemental_page_table *spt);
bool supplemental_page_table_copy (struct supplemental_page_table *dst,
        struct supplemental_page_table *src);
void supplemental_page_table_kill (struct supplemental_page_table *spt);
struct page *spt_find_page (struct supplemental_page_table *spt,
        void *va);
bool spt_insert_page (struct supplemental_page_table *spt, struct page *page);
void spt_remove_page (struct supplemental_page_table *spt, struct page *page);

void vm_init (void);
bool vm_try_handle_fault (struct intr_frame *f, void *addr, bool user,
        bool write, bool not_present);

#define vm_alloc_page(type, upage, writable) \
    vm_alloc_page_with_initializer ((type), (upage), (writable), NULL, NULL)
bool vm_alloc_page_with_initializer (enum vm_type type, void *upage,
        bool writable, vm_initializer *init, void *aux);
void vm_dealloc_page (struct page *page);
bool vm_claim_page (void *va);
enum vm_type page_get_type (struct page *page);

struct list swap_table;
struct list frame_table;
struct lock swap_table_lock;
struct lock frame_table_lock;

#endif  /* VM_VM_H */

