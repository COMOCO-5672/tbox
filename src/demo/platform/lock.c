/* ///////////////////////////////////////////////////////////////////////
 * includes
 */
#include "tbox.h"
#include <stdlib.h>

/* ///////////////////////////////////////////////////////////////////////
 * macros
 */

// the loop maxn
#define TB_TEST_LOOP_MAXN 	(20)

// the lock type
//#define TB_TEST_LOCK_MUTEX
#define TB_TEST_LOCK_SPINLOCK
//#define TB_TEST_LOCK_ATOMIC

/* ///////////////////////////////////////////////////////////////////////
 * globals
 */
static __tb_volatile__ tb_atomic_t g_value = 0;

/* ///////////////////////////////////////////////////////////////////////
 * loop
 */
static tb_pointer_t tb_test_mutx_loop(tb_pointer_t data)
{
	// check
	tb_uint32_t self = (tb_uint32_t)tb_thread_self();
	tb_handle_t lock = (tb_handle_t)data; tb_used(lock);
	tb_print("[loop: %x]: init", self);

	// loop
	__tb_volatile__ tb_size_t n = 1000000;
	while (n--)
	{
#if defined(TB_TEST_LOCK_MUTEX)
		{
			// enter
			tb_mutex_enter(lock);

			// value++
			g_value++;

			// leave
			tb_mutex_leave(lock);
		}
#elif defined(TB_TEST_LOCK_SPINLOCK)
		{
			// enter
			tb_spinlock_enter(&lock);

			// value++
			g_value++;

			// leave
			tb_spinlock_leave(&lock);
		}
#elif defined(TB_TEST_LOCK_ATOMIC)
		tb_atomic_fetch_and_inc(&g_value);
#else
		// value++
		g_value++;
#endif
	}

end:
	tb_print("[loop: %x]: exit", self);
	tb_thread_return(tb_null);
	return tb_null;
}

/* ///////////////////////////////////////////////////////////////////////
 * main
 */
tb_int_t main(tb_int_t argc, tb_char_t** argv)
{
	// init tbox
	if (!tb_init(malloc(1024 * 1024), 1024 * 1024)) return 0;

	// init lock
#if defined(TB_TEST_LOCK_MUTEX)
	tb_handle_t lock = tb_mutex_init();
	tb_assert_and_check_goto(lock, end);
#elif defined(TB_TEST_LOCK_SPINLOCK)
	tb_handle_t lock = TB_SPINLOCK_INIT;
#elif defined(TB_TEST_LOCK_ATOMIC)
	tb_handle_t lock = tb_null; 
#else
	tb_handle_t lock = tb_null; 
#endif

	// init time
	tb_hong_t 	time = tb_mclock();

	// init loop
	tb_size_t 	i = 0;
	tb_size_t 	n = argv[1]? tb_atoi(argv[1]) : TB_TEST_LOOP_MAXN;
	tb_handle_t loop[TB_TEST_LOOP_MAXN] = {0};
	for (i = 0; i < n; i++)
	{
		loop[i] = tb_thread_init(tb_null, tb_test_mutx_loop, lock, 0);
		tb_assert_and_check_goto(loop[i], end);
	}

end:
	// exit thread
	for (i = 0; i < TB_TEST_LOOP_MAXN; i++)
	{
		// kill thread
		if (loop[i]) 
		{
			if (!tb_thread_wait(loop[i], -1))
				tb_thread_kill(loop[i]);
			tb_thread_exit(loop[i]);
			loop[i] = tb_null;
		}

		// exit lock
#if defined(TB_TEST_LOCK_MUTEX)
		if (lock) tb_mutex_exit(lock);
		lock = tb_null;
#elif defined(TB_TEST_LOCK_SPINLOCK)
		tb_spinlock_exit(&lock);
#elif defined(TB_TEST_LOCK_ATOMIC)
		lock = tb_null;
#else
		lock = tb_null;
#endif
	}

	// exit time
	time = tb_mclock() - time;

	// trace
	tb_print("time: %lld ms", time);

	// exit tbox
	tb_exit();
	return 0;
}