#ifndef __LINUX_CPX_ATOMIC_H__
#define __LINUX_CPX_ATOMIC_H__

//  GCC 4.1 builtin atomic operations 
// 使用GCC内置atomic操作
typedef long        atomic_int_t;
typedef unsigned    atomic_uint_t;

typedef volatile atomic_uint_t atomic_t;

#define cpx_atomic_cmp_set(lock, old, set)  \
    __sync_bool_compare_and_swap(lock, old, set)

#define cpx_atomic_fetch_add(value, add)    \
    __sync_fetch_and_add(value, add)

#define cpx_cpu_pause()         __asm__ ("pause")

#define cpx_memory_barrier()    __sync_synchronize()


#endif // __LINUX_CPX_ATOMIC_H__
