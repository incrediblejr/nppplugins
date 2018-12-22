#include "thread.h"
#include <assert.h>
#include <Windows.h>
#include <process.h> // _beginthreadex

namespace npp {

	struct Thread {
		Thread() {}
		operator HANDLE() const	{ return handle; }

		DWORD thread_id;

		ThreadEntry entry_point;
		void *user_data;
		HANDLE handle;
	};


	Thread *thread_create(ThreadEntry f, void *user_data) {
		Thread *t = new Thread();

		t->entry_point = f;
		t->user_data = user_data;
		unsigned int tid;
		HANDLE h = (HANDLE)_beginthreadex(0, 0, f, user_data, CREATE_SUSPENDED, &tid);
		t->thread_id = tid;
		t->handle = h;
		if(!h) {
			delete t;
			t = 0;
		}
		return t;
	}

	void thread_destroy(Thread *t)
	{
		delete t;
	}

	bool thread_wait(Thread *t, unsigned ms)
	{
		return ::WaitForSingleObject((*t), ms) == WAIT_OBJECT_0;
	}

	bool thread_resume(Thread *t) {
		if(t->handle) {
			DWORD res = ::ResumeThread(*t);
			return res != (DWORD) -1;
		}
		assert(false);
		return false;
	}

	bool thread_start(Thread *t) {
		return thread_resume(t);
	}

	bool thread_stop(Thread *t) {
		BOOL res = ::CloseHandle(*t);

		bool success (res != 0);

		if(success)
			t->handle = 0;

		return success;
	}

	bool thread_valid(Thread *t) {
		return (t->handle != 0);
	}
}
