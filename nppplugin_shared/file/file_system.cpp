#include "file_system.h"
#include <Windows.h>

namespace npp {
	namespace file_system {

		unsigned filesize(const wchar_t *path)
		{
			HANDLE h = CreateFile(path, GENERIC_READ, FILE_SHARE_READ, NULL,
									OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

			if(h == INVALID_HANDLE_VALUE)
				return 0;

			unsigned length = 0;
			LARGE_INTEGER size64;
			if(GetFileSizeEx(h, &size64) && size64.HighPart == 0)
				length = size64.LowPart;

			CloseHandle(h);
			return length;
		}

		unsigned read(const wchar_t *path, void *destination, unsigned destination_size)
		{
			HANDLE h = CreateFile(path, GENERIC_READ, FILE_SHARE_READ, NULL,
									OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
			if(h == INVALID_HANDLE_VALUE)
				return 0;

			LARGE_INTEGER size64;
			if(!GetFileSizeEx(h, &size64) || size64.HighPart != 0) {
				CloseHandle(h);
				return 0;
			}

			DWORD len = size64.LowPart;
			if(len > destination_size)
				CloseHandle(h);

			unsigned read_bytes = 0;

			SYSTEM_INFO si;
			GetSystemInfo(&si);
			DWORD read;
			if (ReadFile(h, destination, len, &read, 0) && read == len) {
				read_bytes = read;
			}
			CloseHandle(h);

			return read_bytes;
		}

		unsigned write(const wchar_t *path, const void *data, unsigned data_size)
		{
			HANDLE h = CreateFile(path, GENERIC_WRITE, FILE_SHARE_READ, NULL,
									CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
			if (h == INVALID_HANDLE_VALUE)
				return 0;

			DWORD written;
			BOOL ok = WriteFile(h, data, DWORD(data_size & MAXDWORD), &written, NULL);

			unsigned res = ((ok && (written == data_size)) ? data_size : 0);
			CloseHandle(h);

			return res;
		}

	} // filesystem
} // npp
