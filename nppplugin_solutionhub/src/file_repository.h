#pragma once

typedef void* FileRepositoryHandle;

namespace Json {
	class Value;
}

namespace filerepo {
	FileRepositoryHandle allocate_handle();
	void stop(FileRepositoryHandle&);
	void add_solution(FileRepositoryHandle, Json::Value const&);

	// userdata, buffer, buffersize
	typedef void (*search_callback)(void*, void*, unsigned);
	void search(FileRepositoryHandle, const wchar_t *search_string, void *search_callback_userdata, search_callback);
}
