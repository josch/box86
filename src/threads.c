#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

#include "debug.h"
#include "box86context.h"
#include "threads.h"
#include "x86emu_private.h"
#include "bridge_private.h"
#include "x86run.h"
#include "x86emu.h"
#include "stack.h"
#include "callback.h"
#include "khash.h"
#include "x86run_private.h"
#include "x86trace.h"

// memory handling to be perfected...
// keep a hash thread_t -> emu to set emu->quit to 1 on pthread_cancel

static void pthread_clean_routine(void* p)
{
	x86emu_t *emu = (x86emu_t*)p;

	FreeX86Emu(&emu);
}

static void* pthread_routine(void* p)
{
	void* r = NULL;
	
	pthread_cleanup_push(pthread_clean_routine, p);
	x86emu_t *emu = (x86emu_t*)p;
	Run(emu);
	r = (void*)GetEAX(emu);
	pthread_cleanup_pop(1);

	return r;
}

int EXPORT my_pthread_create(x86emu_t *emu, void* t, void* attr, void* start_routine, void* arg)
{
	int stacksize = 2*1024*1024;	//default stack size is 2Mo
	// TODO: get stack size inside attr
	void* stack = calloc(1, stacksize);
	x86emu_t *emuthread = NewX86Emu(emu->context, (uintptr_t)start_routine, (uintptr_t)stack, 
		stacksize);
	SetupX86Emu(emuthread, emu->shared_global, emu->globals);
	emuthread->trace_start = emu->trace_start;
	emuthread->trace_end = emu->trace_end;
	Push(emuthread, (uintptr_t)arg);
	PushExit(emuthread);
	// create thread
	return pthread_create((pthread_t*)t, (const pthread_attr_t *)attr, 
		pthread_routine, emuthread);
}




KHASH_MAP_INIT_INT(once, int)

#define nb_once	16
typedef void(*thread_once)(void);
typedef void(*key_dtor)(void*);
static x86emu_t *once_emu[nb_once] = {0};
static x86emu_t *dtor_emu[nb_once] = {0};
static void thread_once_callback(int n)
{
	if(once_emu[n]) {
		RunCallback(once_emu[n]);
		FreeCallback(once_emu[n]);
		once_emu[n] = NULL;
	}
}
static void key_dtor_callback(int n, void* a)
{
	if(dtor_emu[n]) {
		SetCallbackArg(dtor_emu[n], 0, a);
		RunCallback(dtor_emu[n]);
	}
}
#define GO(N) \
void key_dtor_callback_##N(void* a) \
{ \
	key_dtor_callback(N, a); \
} \
void thread_once_callback_##N() \
{ \
	thread_once_callback(N); \
}
GO(0)
GO(1)
GO(2)
GO(3)
GO(4)
GO(5)
GO(6)
GO(7)
GO(8)
GO(9)
GO(10)
GO(11)
GO(12)
GO(13)
GO(14)
GO(15)
#undef GO
static const thread_once once_cb[nb_once] = {
	 thread_once_callback_0, thread_once_callback_1, thread_once_callback_2, thread_once_callback_3
	,thread_once_callback_4, thread_once_callback_5, thread_once_callback_6, thread_once_callback_7
	,thread_once_callback_8, thread_once_callback_9, thread_once_callback_10,thread_once_callback_11
	,thread_once_callback_12,thread_once_callback_13,thread_once_callback_14,thread_once_callback_15
};
static const key_dtor dtor_cb[nb_once] = {
	 key_dtor_callback_0, key_dtor_callback_1, key_dtor_callback_2, key_dtor_callback_3
	,key_dtor_callback_4, key_dtor_callback_5, key_dtor_callback_6, key_dtor_callback_7
	,key_dtor_callback_8, key_dtor_callback_9, key_dtor_callback_10,key_dtor_callback_11
	,key_dtor_callback_12,key_dtor_callback_13,key_dtor_callback_14,key_dtor_callback_15
};
kh_once_t 	*once_map = NULL;
// TODO: put all this in libpthread private stuff...

int EXPORT my_pthread_once(x86emu_t* emu, void* once, void* cb)
{
	// look if that once already is in map
	pthread_mutex_lock(&emu->context->mutex_once);
	if(!once_map) {
		once_map = kh_init(once);
	}
	khint_t k;
	k = kh_get(once, once_map, (uintptr_t)once);
	if(k!=kh_end(once_map)) {
			pthread_mutex_unlock(&emu->context->mutex_once);
			return pthread_once(once, once_cb[kh_value(once_map, k)]);
	}
	// look for a free slot
	for(int i=0; i<nb_once; ++i)
		if(!once_emu[i]) {
			once_emu[i] = AddCallback(emu, (uintptr_t)cb, 0, NULL, NULL, NULL, NULL);
			int ret;
			k = kh_put(once, once_map, (uintptr_t)once, &ret);	// add to map
			kh_value(once_map, k) = i;
			pthread_mutex_unlock(&emu->context->mutex_once);
			return pthread_once(once, once_cb[i]);
		}
	printf_log(LOG_NONE, "Warning, no more slot on pthread_once");
	pthread_mutex_unlock(&emu->context->mutex_once);
	return 0;
}

EXPORT int my_pthread_key_create(x86emu_t* emu, void* key, void* dtor)
{
	if(!dtor)
		return pthread_key_create((pthread_key_t*)key, NULL);
	int n = 0;
	while (n<nb_once) {
		if(!dtor_emu[n] || GetCallbackAddress(dtor_emu[n])==((uintptr_t)dtor)) {
			if(!dtor_emu[n]) 
				dtor_emu[n] = AddCallback(emu, (uintptr_t)dtor, 1, NULL, NULL, NULL, NULL);
			return pthread_key_create((pthread_key_t*)key, dtor_cb[n]);
		}
		++n;
	}
	printf_log(LOG_NONE, "Error: pthread_key_create with destructor: no more slot!\n");
	emu->quit = 1;
	return -1;
}
EXPORT int my___pthread_key_create(x86emu_t* emu, void* key, void* dtor) __attribute__((alias("my_pthread_key_create")));

// phtread_cond_init with null attr seems to only take 1 dword on x86, while it 48 bytes on ARM. 
// Not sure why as sizeof(pthread_cond_init) is 48 on both platform... But Neverwinter Night init seems to rely on that

EXPORT int my_pthread_cond_broadcast(x86emu_t* emu, void* cond)
{
	pthread_cond_t * c = *(pthread_cond_t **)cond;
	return pthread_cond_broadcast(c);
}
EXPORT int my_pthread_cond_destroy(x86emu_t* emu, void* cond)
{
	pthread_cond_t * c = *(pthread_cond_t **)cond;
	int ret = pthread_cond_destroy(c);
	free(c);
	return ret;
}
EXPORT int my_pthread_cond_init(x86emu_t* emu, void* cond, void* attr)
{
	pthread_cond_t * c = *(pthread_cond_t **)cond = (pthread_cond_t *)calloc(1, sizeof(pthread_cond_t));
	return pthread_cond_init(c, (const pthread_condattr_t*)attr);
}
EXPORT int my_pthread_cond_signal(x86emu_t* emu, void* cond)
{
	pthread_cond_t * c = *(pthread_cond_t **)cond;
	return pthread_cond_signal(c);
}
EXPORT int my_pthread_cond_timedwait(x86emu_t* emu, void* cond, void* mutex, void* abstime)
{
	pthread_cond_t * c = *(pthread_cond_t **)cond;
	return pthread_cond_timedwait(c, (pthread_mutex_t*)mutex, (const struct timespec*)abstime);
}
EXPORT int my_pthread_cond_wait(x86emu_t* emu, void* cond, void* mutex)
{
	pthread_cond_t * c = *(pthread_cond_t **)cond;
	return pthread_cond_wait(c, (pthread_mutex_t*)mutex);
}
