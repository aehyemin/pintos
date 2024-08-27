#include "devices/timer.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include "threads/interrupt.h"
#include "threads/io.h"
#include "threads/synch.h"
#include "threads/thread.h"

/* See [8254] for hardware details of the 8254 timer chip. */
//타이머의 주파수가 적절한 범위인지 확인하고, 조건에 맞지 않으면 에러 발생
#if TIMER_FREQ < 19
#error 8254 timer requires TIMER_FREQ >= 19
#endif
#if TIMER_FREQ > 1000
#error TIMER_FREQ <= 1000 recommended
#endif

/* 시스템이 부팅된 이후 지나간 타이머 틱 수. Number of timer ticks since OS booted. */
static int64_t ticks;

/* Number of loops per timer tick.
   Initialized by timer_calibrate().
   타이머 틱마다 수행할 수프 횟수 */
static unsigned loops_per_tick;

static intr_handler_func timer_interrupt;//타이머 인터럽트를 처리할 함수의 포인터 정의
static bool too_many_loops (unsigned loops);//지정된 루프 수가 한틱이상 걸리는지
static void busy_wait (int64_t loops);//바쁜 대기 함수 선언. 주어진 횟수만큼 루프를 돈다
static void real_time_sleep (int64_t num, int32_t denom);//실제시간 기반 대기함수. 실시간으로 계산된 시간 동안 대기하는 함수의 프로토 타입


/* Sets up the 8254 Programmable Interval Timer (PIT) to
   interrupt PIT_FREQ times per second, and registers the
   corresponding interrupt. 
   8254 PIT는 하드웨어 타이머 칩으로, 일정한 주기로 인터럽트를 발생시킬 수
   있도록 프로그래밍할 수 있다.
   타이머가 초당 PIT_FREG 횟수만큼 인터럽트를 발생시키도록 설정하고,
   상응하는 인터럽트를 등록한다.
   */
void timer_init (void) {//타이머 장치 초기화
	/* 8254 input frequency divided by TIMER_FREQ, rounded to
	   nearest.
	   8254타이머 칩의 입력 주파수를 TIMER+FREQ로, 나누고 반올림한다. */
	uint16_t count = (1193180 + TIMER_FREQ / 2) / TIMER_FREQ;
					//1193180은 8254타이머 칩의 입력 기본 주파수이다
	outb (0x43, 0x34);    /* CW: counter 0, LSB then MSB, mode 2, binary. */
	outb (0x40, count & 0xff);
	outb (0x40, count >> 8);

	intr_register_ext (0x20, timer_interrupt, "8254 Timer");
}

/* Calibrates loops_per_tick, used to implement brief delays. */
void
timer_calibrate (void) {
	unsigned high_bit, test_bit;

	ASSERT (intr_get_level () == INTR_ON);
	printf ("Calibrating timer...  ");

	/* Approximate loops_per_tick as the largest power-of-two
	   still less than one timer tick. */
	loops_per_tick = 1u << 10;
	while (!too_many_loops (loops_per_tick << 1)) {
		loops_per_tick <<= 1;
		ASSERT (loops_per_tick != 0);
	}

	/* Refine the next 8 bits of loops_per_tick. */
	high_bit = loops_per_tick;
	for (test_bit = high_bit >> 1; test_bit != high_bit >> 10; test_bit >>= 1)
		if (!too_many_loops (high_bit | test_bit))
			loops_per_tick |= test_bit;

	printf ("%'"PRIu64" loops/s.\n", (uint64_t) loops_per_tick * TIMER_FREQ);
}

/* Returns the number of timer ticks since the OS booted. */
int64_t
timer_ticks (void) {
	enum intr_level old_level = intr_disable ();
	int64_t t = ticks;
	intr_set_level (old_level);
	barrier ();
	return t;
}
// 잠든 스레드가 깰 시간(ticks)에 도달할 때까지 ready_list에 추가X,
//깰 시간(ticks)에 도달한 경우에만 ready_list에 추가



/* Returns the number of timer ticks elapsed since THEN, which
   should be a value once returned by timer_ticks(). */
int64_t
timer_elapsed (int64_t then) {
	return timer_ticks () - then;
}

/* Suspends execution for approximately TICKS timer ticks. */
//특정시간(start)로부터 일정 시간 ticks만큼 지나기 전까지 CPU를 양보하고
//스레드를 활성화시키지 않는다
//스레드를 ticks동안 잠재움
void
timer_sleep (int64_t ticks) {
	int64_t start = timer_ticks ();

	ASSERT (intr_get_level () == INTR_ON);
	thread_sleep(start + ticks);

	//while (timer_elapsed (start) < ticks)
		//thread_yield ();
}

//잠든 스레드를 sleep_list에 삽입
//스레드 구조체가 깨어날 시각인 ticks를 저장
//sleep_list에 ticks가 작은 스레드가 앞에 오도록 정렬 삽입
//현재 thread는 잠들어야 하니 thread_block()
void thread_sleep(int64_t ticks) {
	struct thread *curr = thread_current ();//현재 실행중인 스레드
	enum intr_level old_level;//인터럽트 level:on/off. 두꺼비집 찾기

	ASSERT (!intr_context ());//외부 인터럽트가 들어왔으면 True, 아니면 False

	old_level = intr_disable (); //두꺼비집 끄기
	
	curr->wakeup = ticks;
	list_push_back (&sleep_list, &curr->elem);//ready리스트 맨 마지막에 curr
	//do_schedule (THREAD_READY);//컨텍스트 스위칭,  running->ready
	thread_block();
	intr_set_level (old_level); //두꺼비집 키기
}

void thread_awake(int64_t wakeup) {
	struct list_elem *e = list_begin(&sleep_list);
	while (e != list_end(&sleep_list) ) {
		struct thread *t = list_entry(e, struct thread, elem);
		if (t->wakeup <= ticks) {//
		e = list_remove(e);
		thread_unblock(t);
		} else {
			e = list_next(e);
		}
	}

}

//  * struct list_elem *e;

//  * for (e = list_begin (&foo_list); e != list_end (&foo_list);
//  * e = list_next (e)) {
//  *   struct foo *f = list_entry (e, struct foo, elem);
//  *   ...do something with f...
//  * }


/* Suspends execution for approximately MS milliseconds. */
void
timer_msleep (int64_t ms) {
	real_time_sleep (ms, 1000);
}

/* Suspends execution for approximately US microseconds. */
void
timer_usleep (int64_t us) {
	real_time_sleep (us, 1000 * 1000);
}

/* Suspends execution for approximately NS nanoseconds. */
void
timer_nsleep (int64_t ns) {
	real_time_sleep (ns, 1000 * 1000 * 1000);
}

/* Prints timer statistics. */
void
timer_print_stats (void) {
	printf ("Timer: %"PRId64" ticks\n", timer_ticks ());
}





/* Timer interrupt handler. */
static void
timer_interrupt (struct intr_frame *args UNUSED) {
	ticks++;
	thread_tick ();
	thread_awake(ticks);//ticks가 증가할때마다 수행


}

/* Returns true if LOOPS iterations waits for more than one timer
   tick, otherwise false. */
static bool
too_many_loops (unsigned loops) {
	/* Wait for a timer tick. */
	int64_t start = ticks;
	while (ticks == start)
		barrier ();

	/* Run LOOPS loops. */
	start = ticks;
	busy_wait (loops);

	/* If the tick count changed, we iterated too long. */
	barrier ();
	return start != ticks;
}

/* Iterates through a simple loop LOOPS times, for implementing
   brief delays.

   Marked NO_INLINE because code alignment can significantly
   affect timings, so that if this function was inlined
   differently in different places the results would be difficult
   to predict. */
static void NO_INLINE
busy_wait (int64_t loops) {
	while (loops-- > 0)
		barrier ();
}

/* Sleep for approximately NUM/DENOM seconds. */
static void
real_time_sleep (int64_t num, int32_t denom) {
	/* Convert NUM/DENOM seconds into timer ticks, rounding down.

	   (NUM / DENOM) s
	   ---------------------- = NUM * TIMER_FREQ / DENOM ticks.
	   1 s / TIMER_FREQ ticks
	   */
	int64_t ticks = num * TIMER_FREQ / denom;

	ASSERT (intr_get_level () == INTR_ON);
	if (ticks > 0) {
		/* We're waiting for at least one full timer tick.  Use
		   timer_sleep() because it will yield the CPU to other
		   processes. */
		timer_sleep (ticks);
	} else {
		/* Otherwise, use a busy-wait loop for more accurate
		   sub-tick timing.  We scale the numerator and denominator
		   down by 1000 to avoid the possibility of overflow. */
		ASSERT (denom % 1000 == 0);
		busy_wait (loops_per_tick * num / 1000 * TIMER_FREQ / (denom / 1000));
	}
}
