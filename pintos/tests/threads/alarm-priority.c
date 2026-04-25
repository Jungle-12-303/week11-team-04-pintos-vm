/* Checks that when the alarm clock wakes up threads, the
   higher-priority threads run first. */
/* 알람 시계가 스레드를 깨울 때, 우선순위가 높은 스레드가 먼저 실행되는지 확인합니다. */

#include <stdio.h>
#include "tests/threads/tests.h"
#include "threads/init.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "devices/timer.h"

static thread_func alarm_priority_thread;
static int64_t wake_time;
static struct semaphore wait_sema;

void
test_alarm_priority (void) 
{
  int i;
  
  /* This test does not work with the MLFQS. */
  /* 이 테스트는 MLFQS와 함께 작동하지 않습니다. */
  ASSERT (!thread_mlfqs);

  wake_time = timer_ticks () + 5 * TIMER_FREQ;
  sema_init (&wait_sema, 0);
  
  for (i = 0; i < 10; i++) 
    {
      int priority = PRI_DEFAULT - (i + 5) % 10 - 1;
      char name[16];
      snprintf (name, sizeof name, "priority %d", priority);
      thread_create (name, priority, alarm_priority_thread, NULL);
    }

  thread_set_priority (PRI_MIN);

  for (i = 0; i < 10; i++)
    sema_down (&wait_sema);
}

static void
alarm_priority_thread (void *aux UNUSED) 
{
  /* Busy-wait until the current time changes. */
  /* 현재 시간이 변경될 때까지 busy-wait합니다. */
  int64_t start_time = timer_ticks ();
  while (timer_elapsed (start_time) == 0)
    continue;

  /* Now we know we're at the very beginning of a timer tick, so
     we can call timer_sleep() without worrying about races
     between checking the time and a timer interrupt. */
  /* 이제 우리는 타이머 틱의 맨 처음에 있다는 것을 알므로, 시간을 확인하는 것과 타이머 인터럽트 사이의 경쟁 조건을 걱정하지 않고 timer_sleep()을 호출할 수 있습니다. */
  timer_sleep (wake_time - timer_ticks ());

  /* Print a message on wake-up. */
  /* 깨어날 때 메시지를 출력합니다. */
  msg ("Thread %s woke up.", thread_name ());

  sema_up (&wait_sema);
}
