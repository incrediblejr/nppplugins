#pragma once

namespace Json {
	class Value;
}

typedef void* FolderMonitorHandle;
typedef void (*notfify_callback)(void*, void*, unsigned);

namespace folder_monitor {

	// global, help out if(folder monitor does not traverse)
	void add_directory(const wchar_t *d);

	struct RegisterContext {
		void *user_data;
		notfify_callback notify_function;
	};

	FolderMonitorHandle allocate(RegisterContext*);
	void deallocate(FolderMonitorHandle);

	void add_solutions(FolderMonitorHandle, Json::Value const&);

	void start(FolderMonitorHandle);
	void stop(FolderMonitorHandle);

}
