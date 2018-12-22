#include "win_aux.h"
#include <Windows.h>

namespace win_aux {
	std::string get_last_error()
	{
		std::string res;
		DWORD err = ::GetLastError();
		LPSTR s;

		if(::FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER |
			FORMAT_MESSAGE_FROM_SYSTEM,
			NULL,
			err,
			0,
			(LPSTR)&s,
			0,
			NULL)) {
				char *e = strchr(s, '\r');
				if(e)
					*e = 0;
				res = s;
				::LocalFree(s);
		}
		return res;
	}
}
