/*!The Treasure Box Library
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 * 
 * Copyright (C) 2009-2020, TBOOX Open Source Group.
 *
 * @author      ruki
 * @file        poller_process.c
 * @ingroup     platform
 */

/* //////////////////////////////////////////////////////////////////////////////////////
 * includes
 */
#include "prefix.h"
#include "../process.h"
#include "../thread.h"
#include "../atomic.h"
#include "../semaphore.h"
#include "../spinlock.h"
#include "../../algorithm/algorithm.h"
#include "../../container/container.h"

/* //////////////////////////////////////////////////////////////////////////////////////
 * types
 */

// the processes data type
typedef struct __tb_poller_processes_data_t
{
    // the process reference
    tb_process_ref_t        process;

    // the user private data
    tb_cpointer_t           priv;

}tb_poller_processes_data_t;

// the processes status type
typedef struct __tb_poller_processes_status_t
{
    // the process id
    tb_int_t                pid;

    // the process status
    tb_int_t                status;

}tb_poller_processes_status_t;

// the poller process type
typedef struct __tb_poller_process_t
{
    // the main poller
    tb_poller_t*            main_poller;

    // the process poller thread
    tb_thread_ref_t         thread;

    // the waited processes data, pid => process and user private data
    tb_hash_map_ref_t       processes_data;

    // the processes status queue
    tb_vector_ref_t         processes_status;

    // the processes status queue
    tb_vector_ref_t         processes_status_copied;

    // the semaphore
    tb_semaphore_ref_t      semaphore;

    // is stopped?
    tb_atomic32_t           is_stopped;

    // the lock
    tb_spinlock_t           lock;

}tb_poller_process_t; 

/* //////////////////////////////////////////////////////////////////////////////////////
 * private implementation
 */
static tb_int_t tb_poller_process_loop(tb_cpointer_t priv)
{
    // check
    tb_poller_process_t* poller = (tb_poller_process_t*)priv;
    tb_assert_and_check_return_val(poller && poller->semaphore, -1);

    while (!tb_atomic32_get(&poller->is_stopped))
    {
    }

    // mark this thread is stopped
    tb_atomic32_set(&poller->is_stopped, 1);
    return 0;
}

/* //////////////////////////////////////////////////////////////////////////////////////
 * implementation
 */
static tb_void_t tb_poller_process_kill(tb_poller_process_ref_t self)
{
    // check
    tb_poller_process_t* poller = (tb_poller_process_t*)self;
    tb_assert_and_check_return(poller && poller->semaphore);

    // trace
    tb_trace_d("process: kill ..");
}
static tb_void_t tb_poller_process_exit(tb_poller_process_ref_t self)
{
    // check
    tb_poller_process_t* poller = (tb_poller_process_t*)self;
    tb_assert_and_check_return(poller);

    // kill the process poller first
    tb_poller_process_kill(self);

    // exit the process poller thread
    if (poller->thread)
    {
        // wait it
        tb_long_t wait = 0;
        if ((wait = tb_thread_wait(poller->thread, 5000, tb_null)) <= 0)
            tb_trace_e("wait process poller thread failed: %ld!", wait);

        // exit it
        tb_thread_exit(poller->thread);
        poller->thread = tb_null;
    }

    // exit the processes data
    if (poller->processes_data) tb_hash_map_exit(poller->processes_data);
    poller->processes_data = tb_null;

    // exit the processes status
    if (poller->processes_status) tb_vector_exit(poller->processes_status);
    poller->processes_status = tb_null;

    if (poller->processes_status_copied) tb_vector_exit(poller->processes_status_copied);
    poller->processes_status_copied = tb_null;

    // exit lock
    tb_spinlock_exit(&poller->lock);

    // exit poller
    tb_free(poller);
}
static tb_poller_process_ref_t tb_poller_process_init(tb_poller_t* main_poller)
{
    tb_bool_t            ok = tb_false;
    tb_poller_process_t* poller = tb_null;
    do
    {
        // @note only support one process poller instance
        static tb_size_t s_poller_process_num = 0;
        if (s_poller_process_num++)
        {
            tb_trace_e("only support one process poller!");
            break;
        }

        // make the process poller
        poller = tb_malloc0_type(tb_poller_process_t);
        tb_assert_and_check_break(poller);

        // save the main poller
        poller->main_poller = main_poller;

        // init the processes data first
        poller->processes_data = tb_hash_map_init(TB_HASH_MAP_BUCKET_SIZE_MICRO, tb_element_uint32(), tb_element_mem(sizeof(tb_poller_processes_data_t), tb_null, tb_null));
        tb_assert_and_check_break(poller->processes_data);

        // init semaphore
        poller->semaphore = tb_semaphore_init(0);
        tb_assert_and_check_break(poller->semaphore);

        // init lock
        tb_spinlock_init(&poller->lock);

        // init the processes status
        poller->processes_status = tb_vector_init(0, tb_element_mem(sizeof(tb_poller_processes_status_t), tb_null, tb_null));
        tb_assert_and_check_break(poller->processes_status);

        // init the copied processes status
        poller->processes_status_copied = tb_vector_init(0, tb_element_mem(sizeof(tb_poller_processes_status_t), tb_null, tb_null));
        tb_assert_and_check_break(poller->processes_status_copied);

        // start the poller thread for processes first
        poller->thread = tb_thread_init(tb_null, tb_poller_process_loop, poller, 0);
        tb_assert_and_check_break(poller->thread);

        // ok
        ok = tb_true;

    } while (0);

    // failed? exit the poller
    if (!ok)
    {
        if (poller) tb_poller_process_exit((tb_poller_process_ref_t)poller);
        poller = tb_null;
    }
    return (tb_poller_process_ref_t)poller;
}
static tb_void_t tb_poller_process_spak(tb_poller_process_ref_t self)
{
    // check
    tb_poller_process_t* poller = (tb_poller_process_t*)self;
    tb_assert_and_check_return(poller && poller->semaphore);

    // trace
    tb_trace_d("process: spak ..");
}
static tb_bool_t tb_poller_process_insert(tb_poller_process_ref_t self, tb_process_ref_t process, tb_cpointer_t priv)
{
    // check
    tb_poller_process_t* poller = (tb_poller_process_t*)self;
    tb_assert_and_check_return_val(poller && poller->processes_data && process, tb_false);

    // trace
    tb_trace_d("process: insert: %p with priv: %p", process, priv);
    return tb_true;
}
static tb_bool_t tb_poller_process_modify(tb_poller_process_ref_t self, tb_process_ref_t process, tb_cpointer_t priv)
{
    // check
    tb_poller_process_t* poller = (tb_poller_process_t*)self;
    tb_assert_and_check_return_val(poller && poller->processes_data && process, tb_false);

    // trace
    tb_trace_d("process: modify: %p with priv: %p", process, priv);
    return tb_true;
}
static tb_bool_t tb_poller_process_remove(tb_poller_process_ref_t self, tb_process_ref_t process)
{
    // check
    tb_poller_process_t* poller = (tb_poller_process_t*)self;
    tb_assert_and_check_return_val(poller && poller->processes_data && process, tb_false);

    // trace
    tb_trace_d("process: remove: %p", process);
    return tb_true;
}
static tb_bool_t tb_poller_process_wait_prepare(tb_poller_process_ref_t self)
{
    // trace
    tb_trace_d("process: prepare %lu", tb_hash_map_size(((tb_poller_process_t*)self)->processes_data));
    return tb_true;
}
static tb_long_t tb_poller_process_wait_poll(tb_poller_process_ref_t self, tb_poller_event_func_t func)
{
    // check
    tb_poller_process_t* poller = (tb_poller_process_t*)self;
    tb_assert_and_check_return_val(poller && poller->processes_data && func, -1);
    tb_assert_and_check_return_val(poller->processes_status && poller->processes_status_copied, -1);

    return 0;
}