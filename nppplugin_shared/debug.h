#pragma once

#ifdef _DEBUG
	#include <stdio.h>
	#include <stdarg.h>
	#define DEBUG_snprintf(s, n, f, v) _vsnprintf_s((s), (n), _TRUNCATE, (f), (v))

namespace npp {
	inline void debug_print(char const *msg_format, ...)
	{
		#define DEBUG_TEMP_BUFFER_SIZE (2048)
		char buffer[DEBUG_TEMP_BUFFER_SIZE];
		va_list args;

		va_start(args, msg_format);

		if (0 > DEBUG_snprintf(buffer, DEBUG_TEMP_BUFFER_SIZE, msg_format, args))
			buffer[DEBUG_TEMP_BUFFER_SIZE - 1] = 0;

		OutputDebugStringA(buffer);
		va_end(args);

		#undef DEBUG_TEMP_BUFFER_SIZE
	}
}

	#define DEBUG_PRINT(s, ...) npp::debug_print(s "\n", ##__VA_ARGS__)
#else
	#define DEBUG_PRINT
#endif
