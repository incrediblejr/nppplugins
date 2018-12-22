#pragma once
#include <Windows.h>

namespace npp {
	struct CriticalSection {
		CriticalSection() { ::InitializeCriticalSection(&cs); }
		~CriticalSection() { ::DeleteCriticalSection(&cs); }

		void enter() { ::EnterCriticalSection(&cs); }
		void leave() { ::LeaveCriticalSection(&cs); }

	private:
		CRITICAL_SECTION cs;
	};

	struct CriticalSectionScope {
		CriticalSectionScope(CriticalSection &cs) : cs(cs) { cs.enter(); }
		~CriticalSectionScope() { cs.leave(); }

	private:
		CriticalSection &cs;
	};
}
