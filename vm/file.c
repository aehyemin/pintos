/* file.c: Implementation of memory backed file object (mmaped object). */

#include "vm/vm.h"
#include "threads/vaddr.h"
#include "userprog/process.h"

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
void
vm_file_init (void) {
}

/* Initialize the file backed page */
bool
file_backed_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	page->operations = &file_ops;
	struct file_page *file_page = &page->file;

	struct lazy_load_arg *lazy_load_arg = (struct lazy_load_arg *)page->uninit.aux;
    file_page->file = lazy_load_arg->file;
    file_page->ofs = lazy_load_arg->ofs;
    file_page->read_bytes = lazy_load_arg->read_bytes;
    file_page->zero_bytes = lazy_load_arg->zero_bytes;
}

/* Swap in the page by read contents from the file. */
static bool
file_backed_swap_in (struct page *page, void *kva) {
	struct file_page *file_page UNUSED = &page->file;
}

/* Swap out the page by writeback contents to the file. */
static bool
file_backed_swap_out (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;
}

/* Destory the file backed page. PAGE will be freed by the caller. */
//관련 파일을 닫아 파일 지원 페이지를 파괴합니다.
//내용이 dirty인 경우 변경 사항을 파일에 다시 기록해야 합니다. 
//이 함수에서 페이지 구조를 free할 필요는 없습니다.
//file_backed_destroy의 호출자는 이를 처리해야 합니다.

//  bool
// pml4_is_dirty (uint64_t *pml4, const void *vpage) {
// 	uint64_t *pte = pml4e_walk (pml4, (uint64_t) vpage, false);
// 	return pte != NULL && (*pte & PTE_D) != 0;
// } 파일이 수정되었는지 확인

// off_t
// file_write_at (struct file *file, const void *buffer, off_t size,
// 		off_t file_ofs) {
// 	return inode_write_at (file->inode, buffer, size, file_ofs);
// } 지정된 파일의 특정 위치에 데이터를 씀

static void
file_backed_destroy (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;
	if (pml4_is_dirty(thread_current()->pml4, page->va)) {
		file_write_at(file_page->file, page->va, file_page->read_bytes, file_page->ofs);
		pml4_set_dirty(thread_current()->pml4, page->va, 0);
		//변경사항 반영
	}
	//해당 페이지를 “존재하지 않음”으로 표시
	// 이를 통해 이후 해당 페이지에 접근할 때 페이지 폴트가 발생
	pml4_clear_page(thread_current()->pml4, page->va);
}

/* Do the mmap */
//파일을 프로세스의 가상 주소 공간에 메모리 매핑함
void *
do_mmap (void *addr, size_t length, int writable,
		struct file *file, off_t offset) {

	struct file *f = file_reopen(file);
	void *start_addr = addr;//매핑 성공 시 파일이 매핑된 가상 주소반환하는데 사용
	int total_page_count = length <= PGSIZE ? 1: length % PGSIZE ? length / PGSIZE + 1
											   : length / PGSIZE;

	size_t read_bytes = file_length(f) < length ? file_length(f) : length;
	size_t zero_bytes = PGSIZE - read_bytes % PGSIZE;

	ASSERT((read_bytes + zero_bytes) % PGSIZE == 0); // read_bytes + zero_bytes가 페이지 크기(PGSIZE)의 배수인지 확인
	ASSERT(pg_ofs(addr) == 0);						 // upage가 페이지 정렬되어 있는지 확인
	ASSERT(offset % PGSIZE == 0)						 // ofs가 페이지 정렬되어 있는지 확인;

	while (read_bytes > 0 || zero_bytes > 0) // read_bytes와 zero_bytes가 0보다 큰 동안 루프를 실행
	{

		// 이 페이지를 채우는 방법을 계산합니다. 
		//파일에서 PAGE_READ_BYTES 만큼 읽고 나머지 PAGE_ZERO_BYTES 만큼 0으로 채웁니다.
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE; // 최대로 읽을 수 있는 크기는 PGSIZE
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		struct lazy_load_arg *lazy_load_arg = (struct lazy_load_arg *)malloc(sizeof(struct lazy_load_arg));
		lazy_load_arg->file = f;					 // 내용이 담긴 파일 객체
		lazy_load_arg->ofs = offset;					 // 이 페이지에서 읽기 시작할 위치
		lazy_load_arg->read_bytes = page_read_bytes; // 이 페이지에서 읽어야 하는 바이트 수
		lazy_load_arg->zero_bytes = page_zero_bytes; // 이 페이지에서 read_bytes만큼 읽고 공간이 남아 0으로 채워야 하는 바이트 수
		// vm_alloc_page_with_initializer를 호출하여 대기 중인 객체를 생성합니다.
		if (!vm_alloc_page_with_initializer(VM_FILE, addr,
											writable, lazy_load_segment, lazy_load_arg))
			return NULL;

		struct page *p = spt_find_page(&thread_current()->spt, start_addr);
		p->mapped_page_count = total_page_count;
		/* Advance. */
		// 다음 반복을 위하여 읽어들인 만큼 값을 갱신합니다.
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		addr += PGSIZE;
		offset += page_read_bytes;
	}
	return start_addr;
}


/* Do the munmap */
//같은 파일이 매핑된 페이지가 모두 해제될 수 있도록, 매핑할 때 저장해 둔 매핑된 페이지 수를 이용
void
do_munmap (void *addr) {
	struct supplemental_page_table *spt = &thread_current()->spt;
	struct page *p = spt_find_page(spt, addr);
	int count = p->mapped_page_count;
	for (int i=0; i<count; i++) {
		if (p) {
			destroy(p);
		}
		addr += PGSIZE;
		p = spt_find_page(spt, addr);
	}
}
