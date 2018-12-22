#include "folder_monitor.h"

#include "string/string_utils.h"
#include "thread/thread.h"
#include "thread/critical_section.h"

#include "json_aux/json_aux.h"
#include "win32/win_aux.h"
#include "debug.h"

#include "stream.h"
#include "file_repository_common.h"

#include <vector>
#include <string>
#include <map>
#include <algorithm> // std::find
#include <assert.h>

typedef std::wstring String;

typedef std::map<String, bool> DirectoryMap;

namespace internal {
	npp::CriticalSection cs_global_directorymap;
	DirectoryMap global_directorymap;

	bool seen_directory(const String &d)
	{
		npp::CriticalSectionScope holder(cs_global_directorymap);
		return global_directorymap[d];
	}

	void add_directory(const String &d)
	{
		DEBUG_PRINT("[FolderMonitor] internal::add_directory(%S)", d.c_str());
		npp::CriticalSectionScope holder(cs_global_directorymap);
		global_directorymap[d] = true;
	}

	void remove_directory(const String &d)
	{
		DEBUG_PRINT("[FolderMonitor] internal::remove_directory(%S)", d.c_str());
		npp::CriticalSectionScope holder(cs_global_directorymap);
		global_directorymap[d] = false;
	}

	void rename_directory(const String &from, const String &to)
	{
		DEBUG_PRINT("[FolderMonitor] internal::rename_directory(%S, %S)", from.c_str(), to.c_str());
		const unsigned from_length = (unsigned)from.length();

		npp::CriticalSectionScope holder(cs_global_directorymap);
		DirectoryMap::iterator i(global_directorymap.begin()), end(global_directorymap.end());
		while(i!=end)
		{
			if(i->second) {
				const String &dir = i->first;
				unsigned found = (unsigned)dir.find(from);

				if(found == 0) {
					String temp=dir;

					temp.replace(0, from_length, to);
					global_directorymap.insert(DirectoryMap::value_type(temp, true));

					i->second = false;
				}
			}
			++i;
		}
	}
}

namespace {
	// FILE_NOTIFY_CHANGE_ATTRIBUTES|
	// FILE_NOTIFY_CHANGE_LAST_ACCESS|
	// FILE_NOTIFY_CHANGE_SECURITY|
	//
	// FILE_NOTIFY_CHANGE_SIZE|
	// FILE_NOTIFY_CHANGE_CREATION|

	const DWORD DEFAULT_NOTIFY_FLAGS =
		FILE_NOTIFY_CHANGE_FILE_NAME |
		FILE_NOTIFY_CHANGE_DIR_NAME |
		FILE_NOTIFY_CHANGE_LAST_WRITE |
		0;

	const unsigned FM_MAX_PATH = 4096;
	const unsigned FM_MAX_BUFFER = (1024*1024);

	struct MonitorDirectoryData {
		String foldername;
		String include_filter;
		String exclude_filter;

		bool recursive;

		std::vector<String> rename_cache;
	};

	struct DirectoryInformation {
		DirectoryInformation(MonitorDirectoryData const &mdd)
			:handle(INVALID_HANDLE_VALUE)
			,buffer_length(0)
			,directory_data(mdd)
		{
			buffer[0] = 0;
			memset(&overlapped, 0, sizeof(overlapped)); // NTS : This solved issue async failures!
		}

		~DirectoryInformation() {
			if(handle != INVALID_HANDLE_VALUE) {
				::CloseHandle(handle);
			}
		}

		enum {
			MAX_BUFFER = FM_MAX_BUFFER
		};

		HANDLE handle;
		DWORD buffer_length;
		OVERLAPPED overlapped;

		char buffer[FM_MAX_BUFFER];

		MonitorDirectoryData directory_data;
	};

	bool issue_async_watch(DirectoryInformation *di, DWORD notify_filter) {
		bool recursive = di->directory_data.recursive;

		int res = ::ReadDirectoryChangesW(	di->handle,
											di->buffer,
											DirectoryInformation::MAX_BUFFER,
											(BOOL)recursive,
											notify_filter,
											&di->buffer_length,
											&di->overlapped,
											0);

		return (res != 0);
	}

	void extract_changedata(FILE_NOTIFY_INFORMATION *fni, DirectoryInformation *di, std::vector<char> &buffer);

	struct FolderMonitor {
		FolderMonitor(folder_monitor::RegisterContext *ctx)
			:_exit_requested(false)
			,_thread(0)
			,_context(*ctx)
			,io_completion_port(0)
		{}

		~FolderMonitor()
		{
			if(_thread) {
				npp::thread_stop(_thread);
				npp::thread_destroy(_thread);
			}

			for(unsigned i=0; i<directories.size();++i)
				delete directories[i];
		}

		void run()
		{
			unsigned update_index = 0;
			std::vector<char> workbuffer;

			while(true)	{
				workbuffer.clear();

				DWORD num_bytes = 0;
				DirectoryInformation *di = 0;
				OVERLAPPED *overlapped = 0;

				// Retrieve the directory info for this directory
				// through the completion key
				// GetQueuedCompletionStatus will stall until something is available
				BOOL ret = ::GetQueuedCompletionStatus(	io_completion_port,
														&num_bytes,
														(PULONG_PTR) &di,
														&overlapped,
														INFINITE
														);

				if (_exit_requested)
					break;

				if (!ret) {
#ifdef _DEBUG
					DWORD error = ::GetLastError();
					if(error != WAIT_TIMEOUT) {
						std::string s = win_aux::get_last_error();
						DEBUG_PRINT("[FolderMonitor] GetQueuedCompletionStatus failed (%s) ret(%d)", s.c_str(), ret);
					}
#endif
					continue;
				}

				update_index = (update_index+1)%5;
				DEBUG_PRINT("[FolderMonitor] Update, index(%d)", update_index);
				// apparently numBytes can come back 0
				if (di && num_bytes > 0) {
					FILE_NOTIFY_INFORMATION *fni;
					fni = (FILE_NOTIFY_INFORMATION*)di->buffer;

					DWORD offset;
					do {
						extract_changedata(fni, di, workbuffer);
						offset = fni->NextEntryOffset;
						fni = (FILE_NOTIFY_INFORMATION*)((LPBYTE) fni + offset);
					} while(offset);
				} else {
					DEBUG_PRINT("[FolderMonitor] missing DirectoryInformation(%i), num_bytes (%u)", (di ? 0 : 1), (unsigned)num_bytes);
				}

				if (di)	{
					if(!issue_async_watch(di, DEFAULT_NOTIFY_FLAGS)) {
#ifdef _DEBUG
						std::string s = win_aux::get_last_error();
						DEBUG_PRINT("[FolderMonitor] issue_async_watch failed (%s)", s.c_str());
#endif
					}
				} else {
					DEBUG_PRINT("[FolderMonitor] no DirectoryInformation");
				}

				if(!workbuffer.empty()) {
					void *ud = _context.user_data;

					_context.notify_function(ud, &workbuffer[0], (unsigned)workbuffer.size());
				}
			}

			DEBUG_PRINT("[FolderMonitor] Exiting thread func!");
		}

		void setup_filehandles()
		{
			for(unsigned i=0; i<directories.size();++i) {
				DirectoryInformation *di = directories[i];
				const wchar_t *directory = di->directory_data.foldername.c_str();

				HANDLE h = ::CreateFile(directory,
										FILE_LIST_DIRECTORY,
										FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
										0,					// security attributes
										OPEN_EXISTING,		// CreationDisposition
										FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, // FlagsAndAttributes
										0);
				if(h == INVALID_HANDLE_VALUE) {
					DEBUG_PRINT("[FolderMonitor] Failed to create handle for : %S", directory);
				} else {
					DEBUG_PRINT("[FolderMonitor] SUCCESS to create handle for : %S", directory);
				}

				di->handle = h;

				// fileHandle, ExistingCompletionPort,
				// "The per-file completion key that is included in every I/O completion packet for the specified file",
				// NumberOfConcurrentThreads
				io_completion_port = ::CreateIoCompletionPort(h, io_completion_port, (ULONG_PTR) di, 0);

				if (io_completion_port == INVALID_HANDLE_VALUE) {
#ifdef _DEBUG
					std::string s = win_aux::get_last_error();
					DEBUG_PRINT("[FolderMonitor] CreateIoCompletionPort failed (%s)", s.c_str());
#endif
					::CloseHandle(h);
					di->handle = INVALID_HANDLE_VALUE;
				}

				if(!issue_async_watch(di, DEFAULT_NOTIFY_FLAGS)) {
#ifdef _DEBUG
					std::string s = win_aux::get_last_error();
					DEBUG_PRINT("[FolderMonitor] SetupFileHandles failed for %S : error(%s)", directory, s.c_str());
#endif
					::CloseHandle(h);
					di->handle = INVALID_HANDLE_VALUE;
				}
			}
		}

		void start()
		{
			if(_thread)
				return;

			setup_filehandles();

			_thread = npp::thread_create(FolderMonitor::run_tf, this);
			npp::thread_start(_thread);
		}

		void stop()
		{
			_exit_requested = true;
			if(_thread)
				::PostQueuedCompletionStatus(io_completion_port, 0, 0, NULL);
		}

		static unsigned int  __stdcall run_tf(void *d)
		{
			FolderMonitor *fm = (FolderMonitor*)d;
			fm->run();
			delete fm;
			DEBUG_PRINT("[FolderMonitor] thread exiting!");
			return 0;
		}

		void add_directories(Json::Value const &dirs)
		{
			unsigned size = dirs.size();
			while(size) {
				const Json::Value &dir = dirs[--size];
				const char *d = dir["path"].asCString();
				const char *include_filter = (dir["include_filter"].isString() ? dir["include_filter"].asCString() : 0);
				const char *exclude_filter = (dir["exclude_filter"].isString() ? dir["exclude_filter"].asCString() : 0);
				bool recursive = (dir["recursive"].isBool() ? dir["recursive"].asBool() : false);
				bool monitored = (dir["monitored"].isBool() ? dir["monitored"].asBool() : false);

				if(monitored) {
					MonitorDirectoryData mdd;
					mdd.foldername = string_util::to_wide(d);
					file_util::append_slash(mdd.foldername);

					mdd.include_filter = (include_filter ? string_util::to_wide(include_filter) : L"");
					mdd.exclude_filter = (exclude_filter ? string_util::to_wide(exclude_filter) : L"");
					mdd.recursive = recursive;
					directories.push_back(new DirectoryInformation(mdd));
				}
			}
		}

		HANDLE io_completion_port;
		folder_monitor::RegisterContext _context;
		bool _exit_requested;
		npp::Thread *_thread;

		std::vector<DirectoryInformation *> directories;
	};
}

namespace folder_monitor {
	FolderMonitorHandle allocate(RegisterContext *rc)
	{
		return new FolderMonitor(rc);
	}

	void deallocate(FolderMonitorHandle h)
	{
		FolderMonitor *fm = (FolderMonitor*)h;
		if(fm->_thread)
			return stop(h);

		delete fm;
	}

	void stop(FolderMonitorHandle h)
	{
		FolderMonitor *fm = (FolderMonitor*)h;
		if(!fm->_thread)
			return deallocate(h);

		fm->stop();
	}

	void start(FolderMonitorHandle h)
	{
		FolderMonitor *fm = (FolderMonitor*)h;
		fm->start();
	}

	void add_solutions(FolderMonitorHandle h, Json::Value const& dirs)
	{
		FolderMonitor *fm = (FolderMonitor*)h;
		fm->add_directories(dirs);
	}

	void add_directory(const wchar_t *d)
	{
		internal::add_directory(d);
	}

}

namespace {
	const char *fni_action_type(unsigned a) {
		static const char *NOTIFY_ACTION_STRING[] = {
			"FILE_ACTION_ADDED",
			"FILE_ACTION_REMOVED",
			"FILE_ACTION_MODIFIED",
			"FILE_ACTION_RENAMED_OLD_NAME",
			"FILE_ACTION_RENAMED_NEW_NAME",
			"FILE_ACTION_FOLDER_UPDATED"
		};

		a = (a > 5 ? 0 : a);
		return NOTIFY_ACTION_STRING[a];
	}

	bool last_modified_time(FILETIME &res, const wchar_t *full_path)
	{
		WIN32_FIND_DATAW fd;
		HANDLE h = ::FindFirstFileW(full_path, &fd);
		if(h == INVALID_HANDLE_VALUE) {
#ifdef _DEBUG
			std::string s = win_aux::get_last_error();
#endif
			return false;
		}
		res = fd.ftLastWriteTime;
		::FindClose(h);
		return true;
	}

	bool creation_time(FILETIME &res, const wchar_t *full_path)
	{
		WIN32_FIND_DATAW fd;
		HANDLE h = ::FindFirstFileW(full_path, &fd);
		if(h == INVALID_HANDLE_VALUE) {
#ifdef _DEBUG
			std::string s = win_aux::get_last_error();
#endif
			return false;
		}
		res = fd.ftCreationTime;
		::FindClose(h);
		return true;
	}

	void local_time(SYSTEMTIME &lt)
	{
		::GetLocalTime(&lt);
	}

	bool is_directory(const std::wstring &full_path)
	{
		WIN32_FIND_DATAW fd;

		HANDLE h = FindFirstFileW(full_path.c_str(), &fd);
		if(h == INVALID_HANDLE_VALUE) {
#ifdef _DEBUG
			std::string s = win_aux::get_last_error();
			int breakpoint = 0;
			breakpoint = 1;
#endif
		}
		bool res = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
		::FindClose(h);
		return res;
	}

	void extract_changedata(FILE_NOTIFY_INFORMATION *fni, DirectoryInformation *di, std::vector<char> &buffer)
	{
		using namespace npp;

		unsigned faction = fni->Action;
		unsigned action = faction-1;

		bool added = (faction == FILE_ACTION_ADDED) || (faction == FILE_ACTION_RENAMED_NEW_NAME);
		bool removed = !added && ((faction == FILE_ACTION_REMOVED) || (faction == FILE_ACTION_RENAMED_OLD_NAME));
		bool modified = !added && !removed && (faction == FILE_ACTION_MODIFIED);

		//! Extract filename
		const wchar_t *file_name_in = fni->FileName;
		unsigned num_bytes = fni->FileNameLength;
		#if defined _DEBUG
			assert((num_bytes & 1) == 0);
		#endif
		unsigned num_chars = num_bytes / 2;

		std::wstring file_name_wstr(file_name_in, num_chars);
		 // NOTE : 'file_name_in' is the whole buffer!
		const wchar_t *file_name = file_name_wstr.c_str();

		//! Extract fullname
		MonitorDirectoryData &directory_data = di->directory_data;
		std::vector<String> &rc = directory_data.rename_cache;

		std::wstring fullname = directory_data.foldername+file_name_wstr;

		if(removed || modified) {
			if(wcschr(file_name, L'.') == 0) {
				String TEMP = fullname;
				file_util::append_slash(TEMP);
				bool is_seen = internal::seen_directory(TEMP);
				if(is_seen) {
					if(removed) {
						internal::remove_directory(TEMP);
						if(faction == FILE_ACTION_RENAMED_OLD_NAME) {
							// cache and wait for 'FILE_ACTION_RENAMED_NEW_NAME'
							rc.push_back(fullname);
						}
					}
					DEBUG_PRINT("[FolderMonitor] Directory record(%S). Action(%s)", fullname.c_str(), fni_action_type(action));
					return;
				}
			}
		} else if(is_directory(fullname)) {
			if(added) {
				String renamed_dir;

				if(faction == FILE_ACTION_RENAMED_NEW_NAME) {
					String TEMP;
					if(file_util::path(fullname.c_str(), TEMP)) {
						const wchar_t *tcstr = TEMP.c_str();

						std::vector<String>::iterator i(rc.begin());

						while(i != rc.end()) {
							if(string_util::wstristr(i->c_str(), tcstr) != 0) {
								DEBUG_PRINT("FOUND CACHED DIRECTORY RENAMED FROM(%S) -> TO(%S)", i->c_str(), fullname.c_str());

								{
									wchar_t null(0);
									const unsigned sow = sizeof(wchar_t);

									unsigned header = filerepo_headers::CHANGE_DIRECTORY_RENAME;
									unsigned data_size = 42;

									stream::pack(buffer, header);
									unsigned insert_point = (unsigned)buffer.size();

									stream::pack(buffer, data_size);

									String from = *i;
									file_util::append_slash(from);

									unsigned l = (unsigned)from.length();
									stream::pack(buffer, (unsigned)((l+1)*sow));

									stream::pack_bytes(buffer, (const char *)from.c_str(), l*sow);
									stream::pack(buffer, null);

									String to = fullname;
									file_util::append_slash(to);

									l = (unsigned)to.length();
									stream::pack(buffer, (unsigned)((l+1)*sow));

									stream::pack_bytes(buffer, (const char *)to.c_str(), l*sow);
									stream::pack(buffer, null);

									data_size = (unsigned)buffer.size() - insert_point;

									unsigned *patch = (unsigned*)((&buffer[0])+insert_point);

									*patch = data_size;

								}

								renamed_dir = (*i);
								file_util::append_slash(renamed_dir);

								i = rc.erase(i);
							} else {
								++i;
							}

						}
					}
				}
				// append slash after!

				file_util::append_slash(fullname);
				internal::add_directory(fullname);

				if(!renamed_dir.empty()) {
					internal::rename_directory(renamed_dir, fullname);
				}

			}
			DEBUG_PRINT("[FolderMonitor] Directory record(%S), dismissing!. Action(%s)", fullname.c_str(), fni_action_type(action));
			return;
		}

		//! Filter
		//const wchar_t *directory_name = directory_data.foldername.c_str();
		const wchar_t *include_filter = (directory_data.include_filter.empty() ? 0 :  directory_data.include_filter.c_str());
		const wchar_t *exclude_filter = (directory_data.exclude_filter.empty() ? 0 :  directory_data.exclude_filter.c_str());

		if(include_filter || exclude_filter) {
			const wchar_t *fileextension = file_util::fileextension(file_name_wstr.c_str(), false);
			if(fileextension) {
				bool include = true;
				if(include_filter) {
					include = string_util::contains_tokens(fileextension, include_filter, false, L'.');
				} else {
					include = !string_util::contains_tokens(fileextension, exclude_filter, false, L'.');
				}

				if(!include) {
					DEBUG_PRINT("[FolderMonitor] Filter mismatch for file(%S), bailing", file_name_wstr.c_str());
					return;
				}
			}
		}

		//! Make changedata
		wchar_t datestring[17] = {};
		unsigned header;

		unsigned datestring_len = 16;
		if(added || modified) {
			const wchar_t *full_path = fullname.c_str();

			header = (added ? filerepo_headers::CHANGE_ADD : filerepo_headers::CHANGE_UPDATE);

			FILETIME ft;
			bool r = (added ? creation_time(ft, full_path) : last_modified_time(ft, full_path));
			if(r) {
				filerepo::make_internal_datestring_ft(datestring, &ft);
			} else {
				SYSTEMTIME lt;
				local_time(lt);
				filerepo::make_internal_datestring_st(datestring, &lt);
			}
		} else {
			header = filerepo_headers::CHANGE_REMOVE;
			datestring_len = 0;
		}

		const wchar_t *fullname_c = fullname.c_str();
		filerepo::aux::pack_changeheader(buffer, header, fullname_c, datestring_len, datestring);

		#ifdef _DEBUG
			const char *actionstring = fni_action_type(action);
			DEBUG_PRINT("[FolderMonitor] Change (%s) for file (%S), date(%S)\n", actionstring, fullname.c_str(), datestring);
		#endif
	}
}
