#pragma once

namespace npp {
	typedef unsigned int ( __stdcall* ThreadEntry)(void*);

	struct Thread;

	Thread *thread_create(ThreadEntry f, void *user_data);
	void thread_destroy(Thread *t);

	bool thread_wait(Thread *t, unsigned ms);
	bool thread_resume(Thread *t);
	bool thread_start(Thread *t);
	bool thread_stop(Thread *t);
	bool thread_valid(Thread *t);
}
