#pragma once

namespace npp { namespace file_system {

	unsigned filesize(const wchar_t *path);
	unsigned read(const wchar_t *path, void *destination, unsigned destination_size);
	unsigned write(const wchar_t *path, const void *data, unsigned data_size);

} }
