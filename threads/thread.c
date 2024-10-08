#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "intrinsic.h"
#include "threads/fixed_point.h"
#ifdef USERPROG
#include "userprog/process.h"

#endif

struct list ready_list;
struct list sleep_list;

/* Random value for struct thread's `magic' member.
   Used to detect stack overflow.  See the big comment at the top
   of thread.h for details. */
#define THREAD_MAGIC 0xcd6abf4b

/* Random value for basic thread
   Do not modify this value. */
#define THREAD_BASIC 0xd42df210

/* List of processes in THREAD_READY state, that is, processes
   that are ready to run but not actually running. */
//static struct list ready_list;



/* [ sleep list에 있는 알람시간 중 가장 이른 알람시간 ]
   가장 이른 알람시간 ≤ 현재 ticks 이면, 깨울 스레드가 없다는 의미이다. */
extern int64_t MIN_alarm_time;

/* Idle thread. */
static struct thread *idle_thread;

/* Initial thread, the thread running init.c:main(). */
static struct thread *initial_thread;

/* Lock used by allocate_tid(). */
static struct lock tid_lock;

/* Thread destruction requests */
static struct list destruction_req;

/* Statistics. */
static long long idle_ticks;    /* # of timer ticks spent idle. */
static long long kernel_ticks;  /* # of timer ticks in kernel threads. */
static long long user_ticks;    /* # of timer ticks in user programs. */

/* Scheduling. */
#define TIME_SLICE 4            /* # of timer ticks to give each thread. */
static unsigned thread_ticks;   /* # of timer ticks since last yield. */

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
bool thread_mlfqs;
int load_avg;   
#define NICE_DEFAULT 0
#define RECENT_CPU_DEFAULT 0
#define LOAD_AVG_DEFAULT 0
static struct list all_list;

static void kernel_thread (thread_func *, void *aux);

static void idle (void *aux UNUSED);
static struct thread *next_thread_to_run (void);
static void init_thread (struct thread *, const char *name, int priority);
static void do_schedule(int status);
static void schedule (void);
static tid_t allocate_tid (void);

//bool thread_compare_priority(struct list_elem *a, struct list_elem *b, void *aux UNUSED);
// static void check_preemption(void);
/* Returns true if T appears to point to a valid thread. */
#define is_thread(t) ((t) != NULL && (t)->magic == THREAD_MAGIC)

/* Returns the running thread.
 * Read the CPU's stack pointer `rsp', and then round that
 * down to the start of a page.  Since `struct thread' is
 * always at the beginning of a page and the stack pointer is
 * somewhere in the middle, this locates the curent thread. */
#define running_thread() ((struct thread *) (pg_round_down (rrsp ())))


// Global descriptor table for the thread_start.
// Because the gdt will be setup after the thread_init, we should
// setup temporal gdt first.
static uint64_t gdt[3] = { 0, 0x00af9a000000ffff, 0x00cf92000000ffff };

/* Initializes the threading system by transforming the code
   that's currently running into a thread.  This can't work in
   general and it is possible in this case only because loader.S
   was careful to put the bottom of the stack at a page boundary.

   Also initializes the run queue and the tid lock.

   After calling this function, be sure to initialize the page
   allocator before trying to create any threads with
   thread_create().

   It is not safe to call thread_current() until this function
   finishes. */
void
thread_init (void) {//최초의 스레드 부르기
	ASSERT (intr_get_level () == INTR_OFF);

	/* Reload the temporal gdt for the kernel
	 * This gdt does not include the user context.
	 * The kernel will rebuild the gdt with user context, in gdt_init (). */
	struct desc_ptr gdt_ds = {
		.size = sizeof (gdt) - 1,
		.address = (uint64_t) gdt
	};
	lgdt (&gdt_ds);

	/* Init the globla thread context */
	lock_init (&tid_lock);
	list_init (&ready_list);
	list_init (&destruction_req);
	list_init(&sleep_list);
	list_init(&all_list);

	// t->init_prioirty = priority;
	//list_init(&t->donation);
	//t->wait_on_list = NULL;

	/* Set up a thread structure for the running thread. */
	initial_thread = running_thread ();
	init_thread (initial_thread, "main", PRI_DEFAULT);
	list_push_back(&all_list, &(initial_thread->allelem));
	initial_thread->status = THREAD_RUNNING;
	initial_thread->tid = allocate_tid ();

}

/* Starts preemptive thread scheduling by enabling interrupts.
   Also creates the idle thread. */
void
thread_start (void) {
	/* Create the idle thread. */
	struct semaphore idle_started;
	sema_init (&idle_started, 0);
	thread_create ("idle", PRI_MIN, idle, &idle_started);

	//advanced
	load_avg = LOAD_AVG_DEFAULT;

	/* Start preemptive thread scheduling. */
	intr_enable ();

	/* Wait for the idle thread to initialize idle_thread. */
	sema_down (&idle_started);
}

/* Called by the timer interrupt handler at each timer tick.
   Thus, this function runs in an external interrupt context. */
void
thread_tick (void) {
	struct thread *t = thread_current ();

	/* Update statistics. */
	if (t == idle_thread)
		idle_ticks++;
#ifdef USERPROG
	else if (t->pml4 != NULL)
		user_ticks++;
#endif
	else
		kernel_ticks++;

	/* Enforce preemption. */
	if (++thread_ticks >= TIME_SLICE)
		intr_yield_on_return ();
}

/* Prints thread statistics. */
void
thread_print_stats (void) {
	printf ("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
			idle_ticks, kernel_ticks, user_ticks);
}

/* Creates a new kernel thread named NAME with the given initial
   PRIORITY, which executes FUNCTION passing AUX as the argument,
   and adds it to the ready queue.  Returns the thread identifier
   for the new thread, or TID_ERROR if creation fails.

   If thread_start() has been called, then the new thread may be
   scheduled before thread_create() returns.  It could even exit
   before thread_create() returns.  Contrariwise, the original
   thread may run for any amount of time before the new thread is
   scheduled.  Use a semaphore or some other form of
   synchronization if you need to ensure ordering.

   The code provided sets the new thread's `priority' member to
   PRIORITY, but no actual priority scheduling is implemented.
   Priority scheduling is the goal of Problem 1-3. */
tid_t
thread_create (const char *name, int priority,
		thread_func *function, void *aux) {
	struct thread *t;
	tid_t tid;

	ASSERT (function != NULL);

	/* Allocate thread. */
	t = palloc_get_page (PAL_ZERO);
	if (t == NULL)
		return TID_ERROR;

	/* Initialize thread. */
	init_thread (t, name, priority);
	tid = t->tid = allocate_tid ();

	/* Call the kernel_thread if it scheduled.
	 * Note) rdi is 1st argument, and rsi is 2nd argument. */
	t->tf.rip = (uintptr_t) kernel_thread;
	t->tf.R.rdi = (uint64_t) function;
	t->tf.R.rsi = (uint64_t) aux;
	t->tf.ds = SEL_KDSEG;
	t->tf.es = SEL_KDSEG;
	t->tf.ss = SEL_KDSEG;
	t->tf.cs = SEL_KCSEG;
	t->tf.eflags = FLAG_IF;

	list_push_back(&all_list, &t->allelem);
	/* Add to run queue. */
	thread_unblock (t);
	check_preemption();

	/* thread priority */
	// if (t->priority > thread_current()->priority) {
	// 	thread_yield();
	// }

	return tid;
}

/* Puts the current thread to sleep.  It will not be scheduled
   again until awoken by thread_unblock().

   This function must be called with interrupts turned off.  It
   is usually a better idea to use one of the synchronization
   primitives in synch.h. */
void
thread_block (void) {
	ASSERT (!intr_context ());
	ASSERT (intr_get_level () == INTR_OFF);
	thread_current ()->status = THREAD_BLOCKED;
	schedule ();
}

/* Transitions a blocked thread T to the ready-to-run state.
   This is an error if T is not blocked.  (Use thread_yield() to
   make the running thread ready.)

   This function does not preempt the running thread.  This can
   be important: if the caller had disabled interrupts itself,
   it may expect that it can atomically unblock a thread and
   update other data. */
void
thread_unblock (struct thread *t) {
	enum intr_level old_level;

	ASSERT (is_thread (t));

	old_level = intr_disable ();
	ASSERT (t->status == THREAD_BLOCKED);
	//list_push_back (&ready_list, &t->elem);
	list_insert_ordered(&ready_list, &t->elem, thread_compare_priority, 0);
	t->status = THREAD_READY;
	intr_set_level (old_level);
}

/* Returns the name of the running thread. */
const char *
thread_name (void) {
	return thread_current ()->name;
}

/* Returns the running thread.
   This is running_thread() plus a couple of sanity checks.
   See the big comment at the top of thread.h for details. */
struct thread *
thread_current (void) {
	struct thread *t = running_thread ();

	/* Make sure T is really a thread.
	   If either of these assertions fire, then your thread may
	   have overflowed its stack.  Each thread has less than 4 kB
	   of stack, so a few big automatic arrays or moderate
	   recursion can cause stack overflow. */
	ASSERT (is_thread (t));
	ASSERT (t->status == THREAD_RUNNING);

	return t;
}

/* Returns the running thread's tid. */
tid_t
thread_tid (void) {
	return thread_current ()->tid;
}

/* Deschedules the current thread and destroys it.  Never
   returns to the caller. */
void
thread_exit (void) {
	ASSERT (!intr_context ());

#ifdef USERPROG
	process_exit ();
#endif

	/* Just set our status to dying and schedule another process.
	   We will be destroyed during the call to schedule_tail(). */
	list_remove(&thread_current()->allelem); // 이부분 추가
	intr_disable ();
	do_schedule (THREAD_DYING);
	NOT_REACHED ();
}

/* Yields the CPU.  The current thread is not put to sleep and
   may be scheduled again immediately at the scheduler's whim. */
void
thread_yield (void) {
	struct thread *curr = thread_current ();//현재 실행중인 스레드
	enum intr_level old_level;//인터럽트 level:on/off

	ASSERT (!intr_context ());//외부 인터럽트가 들어왔으면 True, 아니면 False

	old_level = intr_disable ();
	if (curr != idle_thread)
		list_insert_ordered(&ready_list, &curr->elem, thread_compare_priority, 0);
		//list_push_back (&ready_list, &curr->elem);//ready리스트 맨 마지막에 curr
	do_schedule (THREAD_READY);//컨텍스트 스위칭,  running->ready
	intr_set_level (old_level);
}
//list_insert 함수 두번째 인자에 해당하는 스레드가 첫번째 인자에 해당하는 스레드 앞에옴
/* 스레드의 우선 순위를 비교한다 */


bool thread_compare_priority(struct list_elem *a, struct list_elem *b, void *aux UNUSED) {
	return list_entry(a, struct thread, elem)->priority > list_entry(b, struct thread, elem)->priority;
}


void check_preemption(void){
	//현재 실행중인 스레드 보다 삽입된 스레드의 우선순위가 높으면, CPU yield
	if (!list_empty(&ready_list) &&
	thread_current()->priority < list_entry(list_front(&ready_list), struct thread, elem)->priority) {
	thread_yield();
	}

}

/* Sets the current thread's priority to NEW_PRIORITY.
현재 스레드의 우선 순위를 새 우선순위로 설정한다 */
void
thread_set_priority (int new_priority) {
	//donation을 고려하여 thread_set_priority 수정
	//refresh_priority함수를 사용하여 우선순위 변경으로 인한 donation관련 정보 갱신
	// check_preemption, 사용하여 우선순위 기부 수행, 스케줄링
	// mlfqs 스케줄러 일때 우선순위를 임의로 변경할 수 없도록 한다
	// mlfqs 스케줄러를 활성화하면 thread_mlfqs 변수는 true로 설정됨.

	if (thread_mlfqs) {
		return;
	}
	
	thread_current ()->init_priority = new_priority;
	thread_current ()->priority = new_priority;

	refresh_priority();
	check_preemption();
}

/* Returns the current thread's priority. 
현재 스레드의 우선 순위를 반환한다. */
int
thread_get_priority (void) {
	return thread_current ()->priority;
}

/* Sets the current thread's nice value to NICE. */
void
thread_set_nice (int nice UNUSED) {
	// 현재 스레드의 nice값을 변경하는 함수를 구현한다.
	// 해당 작업중에 인터럽트는 비활성화 해야한다
	// 현재 스레드의 nice값을 변경한다
	// nice 값 변경 후에 현재 스레드의 우선순위를 재계산하고 
	// 우선순위에 의해 스케줄링한다
	struct thread *t = thread_current();
	enum intr_level old_level;

	old_level = intr_disable();
	t->nice = nice;
	mlfqs_priority(t);
	check_preemption();
	intr_set_level(old_level);
}

/* Returns the current thread's nice value. */
int
thread_get_nice (void) {
	// 현재 스레드의 nice값을 반환한다.
	// 해당 작업중에 인터럽트는 비활성화되어야한다
	struct thread *t = thread_current();
	enum intr_level old_level;

	old_level = intr_disable();
	int nice_val = t->nice;
	intr_set_level(old_level);
	return nice_val;
}

/* Returns 100 times the system load average. */
int
thread_get_load_avg (void) {
	// load_avg에 100을 곱해서 반환
	// 인터럽트 비활성화
	enum intr_level old_level;

	old_level = intr_disable();
	int new_load_avg = fp_to_int(mult_mixed(load_avg, 100));
	intr_set_level(old_level);
	return new_load_avg;
}

/* Returns 100 times the current thread's recent_cpu value. */
int
thread_get_recent_cpu (void) {
	// recent_cpu에 100을 곱해서 반환
	// 인터럽트 비활성화
	enum intr_level old_level;

	old_level = intr_disable();
	int new_recent_cpu = fp_to_int(mult_mixed(thread_current()->recent_cpu, 100));
	intr_set_level(old_level);
	return new_recent_cpu;
}

/* Idle thread.  Executes when no other thread is ready to run.

   The idle thread is initially put on the ready list by
   thread_start().  It will be scheduled once initially, at which
   point it initializes idle_thread, "up"s the semaphore passed
   to it to enable thread_start() to continue, and immediately
   blocks.  After that, the idle thread never appears in the
   ready list.  It is returned by next_thread_to_run() as a
   special case when the ready list is empty. */
static void
idle (void *idle_started_ UNUSED) {
	struct semaphore *idle_started = idle_started_;

	idle_thread = thread_current ();
	sema_up (idle_started);

	for (;;) {
		/* Let someone else run. */
		intr_disable ();
		thread_block ();

		/* Re-enable interrupts and wait for the next one.

		   The `sti' instruction disables interrupts until the
		   completion of the next instruction, so these two
		   instructions are executed atomically.  This atomicity is
		   important; otherwise, an interrupt could be handled
		   between re-enabling interrupts and waiting for the next
		   one to occur, wasting as much as one clock tick worth of
		   time.

		   See [IA32-v2a] "HLT", [IA32-v2b] "STI", and [IA32-v3a]
		   7.11.1 "HLT Instruction". */
		asm volatile ("sti; hlt" : : : "memory");
	}
}

/* Function used as the basis for a kernel thread. */
static void
kernel_thread (thread_func *function, void *aux) {
	ASSERT (function != NULL);

	intr_enable ();       /* The scheduler runs with interrupts off. */
	function (aux);       /* Execute the thread function. */
	thread_exit ();       /* If function() returns, kill the thread. */
}


/* Does basic initialization of T as a blocked thread named
   NAME. */
static void
init_thread (struct thread *t, const char *name, int priority) {//스레드 만들기
	ASSERT (t != NULL);
	ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);
	ASSERT (name != NULL);

	memset (t, 0, sizeof *t);
	t->status = THREAD_BLOCKED;
	strlcpy (t->name, name, sizeof t->name);
	t->tf.rsp = (uint64_t) t + PGSIZE - sizeof (void *);
	t->priority = priority;
	t->magic = THREAD_MAGIC;

    t->init_priority = priority;
	t->wait_on_lock = NULL;
	list_init(&t->donations);
	//advanced
	t->nice = NICE_DEFAULT;
    t->recent_cpu = RECENT_CPU_DEFAULT;

}

/* Chooses and returns the next thread to be scheduled.  Should
   return a thread from the run queue, unless the run queue is
   empty.  (If the running thread can continue running, then it
   will be in the run queue.)  If the run queue is empty, return
   idle_thread. */
static struct thread *
next_thread_to_run (void) {
	if (list_empty (&ready_list))
		return idle_thread;
	else
		return list_entry (list_pop_front (&ready_list), struct thread, elem);
}

/* Use iretq to launch the thread */
void
do_iret (struct intr_frame *tf) {
	__asm __volatile(
			"movq %0, %%rsp\n"
			"movq 0(%%rsp),%%r15\n"
			"movq 8(%%rsp),%%r14\n"
			"movq 16(%%rsp),%%r13\n"
			"movq 24(%%rsp),%%r12\n"
			"movq 32(%%rsp),%%r11\n"
			"movq 40(%%rsp),%%r10\n"
			"movq 48(%%rsp),%%r9\n"
			"movq 56(%%rsp),%%r8\n"
			"movq 64(%%rsp),%%rsi\n"
			"movq 72(%%rsp),%%rdi\n"
			"movq 80(%%rsp),%%rbp\n"
			"movq 88(%%rsp),%%rdx\n"
			"movq 96(%%rsp),%%rcx\n"
			"movq 104(%%rsp),%%rbx\n"
			"movq 112(%%rsp),%%rax\n"
			"addq $120,%%rsp\n"
			"movw 8(%%rsp),%%ds\n"
			"movw (%%rsp),%%es\n"
			"addq $32, %%rsp\n"
			"iretq"
			: : "g" ((uint64_t) tf) : "memory");
}

/* Switching the thread by activating the new thread's page
   tables, and, if the previous thread is dying, destroying it.

   At this function's invocation, we just switched from thread
   PREV, the new thread is already running, and interrupts are
   still disabled.

   It's not safe to call printf() until the thread switch is
   complete.  In practice that means that printf()s should be
   added at the end of the function. */
static void
thread_launch (struct thread *th) {
	uint64_t tf_cur = (uint64_t) &running_thread ()->tf;
	uint64_t tf = (uint64_t) &th->tf;
	ASSERT (intr_get_level () == INTR_OFF);

	/* The main switching logic.
	 * We first restore the whole execution context into the intr_frame
	 * and then switching to the next thread by calling do_iret.
	 * Note that, we SHOULD NOT use any stack from here
	 * until switching is done. */
	__asm __volatile (
			/* Store registers that will be used. */
			"push %%rax\n"
			"push %%rbx\n"
			"push %%rcx\n"
			/* Fetch input once */
			"movq %0, %%rax\n"
			"movq %1, %%rcx\n"
			"movq %%r15, 0(%%rax)\n"
			"movq %%r14, 8(%%rax)\n"
			"movq %%r13, 16(%%rax)\n"
			"movq %%r12, 24(%%rax)\n"
			"movq %%r11, 32(%%rax)\n"
			"movq %%r10, 40(%%rax)\n"
			"movq %%r9, 48(%%rax)\n"
			"movq %%r8, 56(%%rax)\n"
			"movq %%rsi, 64(%%rax)\n"
			"movq %%rdi, 72(%%rax)\n"
			"movq %%rbp, 80(%%rax)\n"
			"movq %%rdx, 88(%%rax)\n"
			"pop %%rbx\n"              // Saved rcx
			"movq %%rbx, 96(%%rax)\n"
			"pop %%rbx\n"              // Saved rbx
			"movq %%rbx, 104(%%rax)\n"
			"pop %%rbx\n"              // Saved rax
			"movq %%rbx, 112(%%rax)\n"
			"addq $120, %%rax\n"
			"movw %%es, (%%rax)\n"
			"movw %%ds, 8(%%rax)\n"
			"addq $32, %%rax\n"
			"call __next\n"         // read the current rip.
			"__next:\n"
			"pop %%rbx\n"
			"addq $(out_iret -  __next), %%rbx\n"
			"movq %%rbx, 0(%%rax)\n" // rip
			"movw %%cs, 8(%%rax)\n"  // cs
			"pushfq\n"
			"popq %%rbx\n"
			"mov %%rbx, 16(%%rax)\n" // eflags
			"mov %%rsp, 24(%%rax)\n" // rsp
			"movw %%ss, 32(%%rax)\n"
			"mov %%rcx, %%rdi\n"
			"call do_iret\n"
			"out_iret:\n"
			: : "g"(tf_cur), "g" (tf) : "memory"
			);
}

/* Schedules a new process. At entry, interrupts must be off.
 * This function modify current thread's status to status and then
 * finds another thread to run and switches to it.
 * It's not safe to call printf() in the schedule(). */
static void
do_schedule(int status) {
	ASSERT (intr_get_level () == INTR_OFF);
	ASSERT (thread_current()->status == THREAD_RUNNING);
	while (!list_empty (&destruction_req)) {//종료예약된스레드
		struct thread *victim =
			list_entry (list_pop_front (&destruction_req), struct thread, elem);
		palloc_free_page(victim);
	}
	thread_current ()->status = status;
	schedule ();
}

static void
schedule (void) {
	struct thread *curr = running_thread ();
	struct thread *next = next_thread_to_run ();

	ASSERT (intr_get_level () == INTR_OFF);
	ASSERT (curr->status != THREAD_RUNNING);
	ASSERT (is_thread (next));
	/* Mark us as running. */
	next->status = THREAD_RUNNING;

	/* Start new time slice. */
	thread_ticks = 0;

#ifdef USERPROG
	/* Activate the new address space. */
	process_activate (next);
#endif

	if (curr != next) {
		/* If the thread we switched from is dying, destroy its struct
		   thread. This must happen late so that thread_exit() doesn't
		   pull out the rug under itself.
		   We just queuing the page free reqeust here because the page is
		   currently used by the stack.
		   The real destruction logic will be called at the beginning of the
		   schedule(). */
		if (curr && curr->status == THREAD_DYING && curr != initial_thread) {
			ASSERT (curr != next);
			list_push_back (&destruction_req, &curr->elem);
		}

		/* Before switching the thread, we first save the information
		 * of current running. */
		thread_launch (next);
	}
}

/* Returns a tid to use for a new thread. */
static tid_t
allocate_tid (void) {
	static tid_t next_tid = 1;
	tid_t tid;

	lock_acquire (&tid_lock);
	tid = next_tid++;
	lock_release (&tid_lock);

	return tid;
}

//nested
//multiple 한번에 여러개 lock이 필요하면 왔다 갔다 한개씩 모두 획득해야 작업시작
void donate_priority(void) {
	//priority donation 을 수행하는 함수를 구현한다.
	//현재 스레드가 기다리고 있는 lock과 연결된 모든 스레드들을 순회하며
	//현재 스레드의 우선순위를 lock을 보유하고 있는 스레드에게 기부한다
	//nested depth 는 8로 제한한다
	int cnt = 0;
	struct thread *t = thread_current();
	int cur_priority = t->priority; //이전 스레드 우선순위 기억

	while (cnt < 9) {
		cnt++;
		if (t->wait_on_lock == NULL) {//현재 스레드가 기다리고 있는 락이 없으면 종료
			break;
		}
		t = t->wait_on_lock->holder; //홀더 전달
		t->priority = cur_priority; //우선순위 기부
	}
}

void remove_with_lock(struct lock *lock) {
	//lock을 해지 했을 때 donations 리스트에서 해당 엔트리를 삭제하기 위한 함수 구현
	//현재 스레드의 donations 리스트를 확인하여 해지 할 lock을 보유하고 잇는 엔트리 삭제
	struct thread *t = thread_current();
	struct list_elem *e = list_begin(&t->donations);
	//리스트의 처음부터 끝까지 확인하여
	for (e; e!=list_end(&t->donations);) {
		struct thread *cur = list_entry(e, struct thread, donation_elem);

		if (cur->wait_on_lock == lock) {
			e = list_remove(e);
		} else {
			e = list_next(e);
		}
	}
}

// void remove_with_lock (struct lock *lock)
// {
// 	struct thread *t = thread_current();
// 	struct list_elem *e = list_begin(&t->donations);
 
// 	for (e ; e != list_end((&t->donations));)
// 	{
// 		struct thread *cur = list_entry(e, struct thread, donation_elem);
// 		if (cur->wait_on_lock == lock)
// 		{
// 			e = list_remove(e);
// 		}
// 		else
// 		{
// 			e = list_next(e);
// 		}
// 	}
// }


void refresh_priority(void) {
	//스레드의 우선순위가 변경 되었을 때 donation을 고려하여 우선순위 다시 결정
	//현재 스레드의 우선순위를 기부받기 전의 우선순위로 변경
	//가장 우선순위가 높은 donations리스트의 스레드와 현재 스레드의 우선순위를 비교하여 높은 값을
	//현재 스레드의 우선 순위로 설정한다
	struct thread *t = thread_current();
	t->priority = t->init_priority;
	//struct list_elem * e = list_begin(&t->donations);
	
	if (list_empty(&t->donations) == false) { //도네이션 리스트가 빌 때까지
		//list_sort(&t->donations, thread_compare_priority, 0);
		struct thread *first = list_entry(list_front(&t->donations), struct thread, donation_elem);
		if (first->priority > t->priority) {
			t->priority = first->priority;
		}
	}
}




void mlfqs_priority (struct thread *t) {
	// 해당 스레드가 idle_thread 가 아닌지 검사
	// priority 계산식을 구현(fixed_point.h의 계산함수 이용)
	if (t != idle_thread) {
		// priority = PRI_MAX - (recent_cpu / 4) - (nice * 2),
		int recent_cpu_by_4 = div_mixed(t->recent_cpu, 4);
		int nice_by_2 = 2 * t->nice;
		int to_sub = add_mixed(recent_cpu_by_4, nice_by_2);
		int sub = sub_mixed(to_sub, (int)PRI_MAX);
		int pri_result = fp_to_int(sub_fp(0, sub));
		if (pri_result < PRI_MIN) {
			pri_result = PRI_MIN;
		} 
		if (pri_result > PRI_MAX) {
			pri_result = PRI_MAX;
		}
		t->priority = pri_result;
	}
}

void mlfqs_recent_cpu (struct thread *t) {
	// 해당 스레드가 idle_thread 가 아닌지 검사
	// recent_cpu 계산식 구현(fixed_point.h의 계산함수 이용)
	if (t != idle_thread) {
//recent_cpu = (2 * load_avg)/(2 * load_avg + 1)* recent_cpu+nice
		int load_avg_2 = mult_mixed(load_avg, 2);//여기반대로씀
		int load_avg_2_1 = add_mixed(load_avg_2, 1);
		int frac = div_fp(load_avg_2, load_avg_2_1);
		int tmp = mult_fp(frac, t->recent_cpu);//mixed라씀
		int result = add_mixed(tmp, t->nice);
		if ((result >> 31) == (-1) >> 31) {
			result = 0;
		}
		t->recent_cpu = result;
	}
}

void mlfqs_load_avg (void) {
	// load_avg 계산식 구현(fixed_point.h이용)
	// load_avg는 0보다 작아질 수 없다
	// load_avg = (59/60) * load_avg + (1/60) * ready_threads
	// readythread는 readylist의 크기
	int a = div_fp(int_to_fp(59), int_to_fp(60));
	int b = div_fp(int_to_fp(1), int_to_fp(60));//여기1이라씀 뒤에를
	int load_avg2 = mult_fp(a, load_avg);//여기a,b라씀
	int ready_thread = (int)list_size(&ready_list);
	ready_thread = (thread_current() == idle_thread) ? ready_thread : ready_thread + 1;
	int ready_thread2 = mult_mixed(b, ready_thread);
	int result = add_fp(load_avg2, ready_thread2);
	load_avg = result;

}

void mlfqs_increment (void) {
	// 해당 스레드가 idle 스레드가 아닌지 검사
	// 현재 스레드의 recent_cpu 값을 1 증가 시킨다
	if (thread_current() != idle_thread) {
		int cur_recent_cpu = thread_current()->recent_cpu;
		thread_current()->recent_cpu = add_mixed(cur_recent_cpu, 1);
	}
}

void mlfqs_recalc_recent_cpu (void) {
	//모든 스레드의 recent_cpu 재계산
	for (struct list_elem *e = list_begin(&all_list); e != list_end(&all_list); e = list_next(e)) {
		mlfqs_recent_cpu(list_entry(e, struct thread, allelem));
	}
}

void mlfqs_recalc_priority (void) {
	//모든 스레드의 priority 값을 재계산한다
	for (struct list_elem *e = list_begin(&all_list); e != list_end(&all_list); e = list_next(e)) {
		mlfqs_priority(list_entry(e, struct thread, allelem));
	}

}