use core::mem::size_of;

use crate::{
    sched::{this_rros_rq},
    thread::{rros_sleep, KthreadRunner},
};
use kernel::{bindings, c_str, c_types, prelude::*, task};

#[allow(dead_code)]
static mut KTHREAD_RUNNER_1: KthreadRunner = KthreadRunner::new_empty();
#[allow(dead_code)]
static mut KTHREAD_RUNNER_2: KthreadRunner = KthreadRunner::new_empty();
static mut PAUSE_FLAG: bool = true;
static mut RESUMED: bool = false;
static mut COMPUTED_OFFSET: bool = false;
static mut THREAD_TO_RESTORE: *mut bindings::task_struct = 0 as *mut bindings::task_struct;

#[allow(dead_code)]
pub fn test_thread_context_switch() {
    let rq = this_rros_rq();

    let _rq_len = unsafe { (*rq).fifo.runnable.head.clone().unwrap().len() };

    unsafe {
        KTHREAD_RUNNER_1.init(Box::try_new(kfn_1).unwrap());
        KTHREAD_RUNNER_2.init(Box::try_new(kfn_2).unwrap());
        KTHREAD_RUNNER_1.run(c_str!("kthread_1"));
        KTHREAD_RUNNER_2.run(c_str!("kthread_2"));
    }
}

#[allow(dead_code)]
fn kfn_1() {
    unsafe {
        while !bindings::is_crash_kernel() || !COMPUTED_OFFSET {
            rros_sleep(1000_000_000).unwrap();
        }
        bindings::rros_restore_thread(THREAD_TO_RESTORE);
        PAUSE_FLAG = false;
        pr_info!("kfn_1 call rros_restore_thread finished\n");
    }
}

#[allow(dead_code)]
pub fn kfn_2() {
    let mut sum = 0;
    let mut i = 0;

    unsafe {
        let sum_addr = &sum as *const _ as c_types::c_ulong;
        let i_addr = &i as *const _ as c_types::c_ulong;
        let addr = [sum_addr, i_addr];
        let task_stack = (*task::Task::current_ptr()).stack as c_types::c_ulong;
        for j in 0..2 {
            let index = bindings::var_num as usize + j as usize;
            bindings::offset[index] = addr[j] - task_stack;
            bindings::size[index] = size_of::<i32>() as u64;
            pr_info!(
                "offset[{}] = {}, size[{}] = {}\n, addr[{}] = {}",
                index,
                bindings::offset[index],
                index,
                bindings::size[index],
                j,
                addr[j]
            );
        }
        bindings::var_num += 2;
        COMPUTED_OFFSET = true;
        THREAD_TO_RESTORE = task::Task::current_ptr();
    }

    while i < 1000 {
        if i <= 300 {
            sum += i;
            unsafe {
                if i == 300 && !bindings::is_crash_kernel() {
                    sum = 0xdead;
                    bindings::preserve_context();
                    pr_info!("kfn_2: preserve i is {}\n", i);
                    pr_info!("kfn_2: preserve sum is {}\n", sum);
                    pr_info!("kfn_2: preserve_context finished\n");
                }
            }
        } else {
            unsafe {
                if i == 500 && !RESUMED {
                    while PAUSE_FLAG {
                        rros_sleep(1_000_000_000).unwrap();
                    }
                    pr_info!("kfn_2 resume, i: {}\n", i);
                    pr_info!("kfn_2 resume, sum: {}\n", sum);
                    RESUMED = true;
                }
            }
            sum += i;
        }
        i += 1;
    }
    pr_info!("kfn_2 finished! sum: {}\n", sum);
}
