#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "threads/init.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "devices/input.h"
#include "threads/synch.h"
#include "string.h"
#include "userprog/process.h"
#include "threads/palloc.h"


typedef int pid_t;
struct lock filesys_lock;

void munmap(void *addr);
void *mmap (void *addr, size_t length, int writable, int fd, off_t offset);

void syscall_entry (void);
void syscall_handler (struct intr_frame *);
void halt (void);
void exit (int status);
pid_t fork (const char *thread_name, struct intr_frame *f);
int exec (const char *cmd_line);
int wait (pid_t pid);
bool create (const char *file, unsigned initial_size);
bool remove (const char *file);
int open (const char *file);
int filesize (int fd);
int read (int fd, void *buffer, unsigned size);
int write (int fd, const void *buffer, unsigned size);
void seek (int fd, unsigned position);
unsigned tell (int fd);
void close (int fd);
int add_file(struct file* f);
struct file *get_file(int fd);
void check_ptr(void *ptr);
bool check_fd(int fd);
/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081         /* Segment selector msr */
#define MSR_LSTAR 0xc0000082        /* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */

void
syscall_init (void) {
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48  |
			((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t) syscall_entry);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);

	lock_init(&filesys_lock);
}

/* The main system call interface */
void
syscall_handler (struct intr_frame *f UNUSED) {
	//printf("syscall_handler 시작\n");  // 이 출력이 되는지 먼저 확인
	int number = f->R.rax;
	//printf("syscall_handler: f = %p, rip = %p, rsp = %p\n", f, f->rip, f->rsp);
#ifdef VM
    thread_current()->rsp = f->rsp; // 추가
#endif
	switch (number)
	{
		case SYS_HALT:
			halt();
			break;
		case SYS_EXIT:
			exit(f->R.rdi);
			break;
		case SYS_FORK:
			f->R.rax = fork(f->R.rdi, f);
			break;
		case SYS_EXEC:
			f->R.rax = exec(f->R.rdi);
			break;
		case SYS_WAIT:
			f->R.rax = wait(f->R.rdi);
			break;
		case SYS_CREATE:
			f->R.rax = create(f->R.rdi, f->R.rsi);
			break;
		case SYS_REMOVE:
			f->R.rax = remove(f->R.rdi);
			break;
		case SYS_OPEN:
			f->R.rax = open(f->R.rdi);
			break;
		case SYS_FILESIZE:
			f->R.rax = filesize(f->R.rdi);
			break;
		case SYS_READ:
			f->R.rax = read(f->R.rdi, f->R.rsi, f->R.rdx);
			break;
		case SYS_WRITE:
			f->R.rax = write(f->R.rdi, f->R.rsi, f->R.rdx);
			break; 
		case SYS_SEEK:
			seek(f->R.rdi, f->R.rsi);
			break;
		case SYS_TELL:
			f->R.rax = tell(f->R.rdi);
			break;
		case SYS_CLOSE:
			close(f->R.rdi);
			break;  
		case SYS_MMAP:
			f->R.rax = mmap(f->R.rdi, f->R.rsi, f->R.rdx, f->R.r10, f->R.r8);
			break;
		case SYS_MUNMAP:
			munmap(f->R.rdi);
			break;
	}
}

void halt (void){
	power_off();
}
void exit (int status){
	thread_current()->exit_status = status;
	thread_current()->is_exit = true;
	thread_exit();
}
pid_t fork (const char *thread_name, struct intr_frame *f){
	return process_fork(thread_name, f);
}
int exec (const char *cmd_line){
	check_ptr(cmd_line);

	if(thread_current()->exec_file != NULL) {
		file_close(thread_current()->exec_file);
		thread_current()->exec_file = NULL;
	}

	char *copy = palloc_get_page(0);
	strlcpy(copy, cmd_line, PGSIZE);
	process_exec(copy);
	exit(-1);
}
int wait (pid_t pid){
	return process_wait(pid);
}
bool create (const char *file, unsigned initial_size){
	if (!file){
	 exit(-1);
	}
	check_ptr(file);
	return filesys_create(file, initial_size);
}
bool remove (const char *file){
	check_ptr(file);
	return filesys_remove(file);
}
int open (const char *file){
	if (file == NULL){
		return -1;}
	check_ptr(file);
	if(file[0] == '\0') return -1;

	struct file *f = filesys_open(file);
	if(f == NULL) return -1;

	int fd = add_file(f);
	if(fd == -1)
		file_close(f);

	return fd;
}
int filesize (int fd){
	if(check_fd(fd)) return -1;
	return file_length(get_file(fd));
}
int read (int fd, void *buffer, unsigned size){
	if(check_fd(fd)) return -1;
	check_ptr(buffer);
	struct page *page = spt_find_page(fd, buffer);

	if(fd == 0){
		for(int i = 0; i < size; i++){
			((char*)buffer)[i] = input_getc();
		}
		return size;
		
	} else if(fd == 1) {
		return -1;

	} else {
		if (page == NULL || !page->writable) {
			exit(-1);
		}
	}
	struct file *f = get_file(fd);
	if(f == NULL) {
		return -1;
		}

	lock_acquire(&filesys_lock);
	int read_file = file_read(f, buffer, size);
	lock_release(&filesys_lock);

}
int write (int fd, const void *buffer, unsigned size){
	if(check_fd(fd)) return -1;
	check_ptr(buffer);

	if(fd == 1){
		putbuf(buffer, size);
		return size;
	}
	else if(fd == 0) return 0;

	struct file *f = get_file(fd);
	if(f == NULL) return -1;
	return file_write(f, buffer, size);
}
void seek (int fd, unsigned position){
	struct file *f = get_file(fd);
	file_seek(f, position);
}
unsigned tell (int fd){
	struct file *f = get_file(fd);
	return file_tell(f);
}
void close (int fd){
	if(check_fd(fd)) return -1;
	struct file *f = get_file(fd);
	if(f == NULL) return -1;

	file_close(f);
	thread_current()->fdt[fd] = NULL;
}

int add_file(struct file* f){
	for(int fd = 3; fd < maxfd; fd++){
		if(thread_current()->fdt[fd] == NULL) 
		{
			thread_current()->fdt[fd] = f;
			return fd;
		}
	}
	return -1;
}

struct file *get_file(int fd){
	return thread_current()->fdt[fd];
}

void check_ptr(void *ptr)
{
	if (ptr == NULL) {
	printf("Invalid pointer access at %p\n", ptr);
		exit(-1);
	}
	if (!is_user_vaddr(ptr)) {
		exit(-1);
    printf("Invalid pointer access at %p\n", ptr);
	}
	// if (pml4_get_page(thread_current()->pml4, addr) == NULL)
	// 	exit(-1);
}


bool check_fd(int fd){
	if(fd < 0 || maxfd <= fd) return true;
	if(fd > 2 && thread_current()->fdt[fd] == NULL) return true;
	return false;
}

//메모리매핑을 통해 파일의 내용을 프로세스의 가상 주소 공간에 직접 매핑하는 시스템 콜
//파일의 내용을 메모리에 로드하지 않고도 파일 데이터를 직접 매핑할 수 있게됨
void *mmap (void *addr, size_t length, int writable, int fd, off_t offset) {
	if (!addr || addr != pg_round_down(addr)) 
		return NULL;
	
	if (offset != pg_round_down(offset))
		return NULL;

	if (!is_user_vaddr(addr) || !is_user_vaddr(addr + length))
		return NULL;

	if (spt_find_page(&thread_current()->spt, addr))
		return NULL;

	struct file *f = get_file(fd);
	if (f == NULL)	
		return NULL;

	if (file_length(f) == 0 || (int)length <= 0)
		return NULL;

	return do_mmap(addr, length, writable, f, offset);
	//파일이 매핑된 가상 주소 변환
}

void munmap(void *addr)
{
    do_munmap(addr);
}

