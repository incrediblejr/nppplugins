#include "file_repository.h"
#include "file_repository_common.h"

#include "json/json.h"
#include "thread/thread.h"
#include "thread/critical_section.h"
#include "debug.h"

#include "stream.h"

#include "string/string_utils.h"

#include "win32/win_aux.h"

#include "folder_monitor.h"

#include <vector>
#include <sstream>

using namespace npp;

typedef std::wstring String;

namespace
{
struct SearchResponseData {
	void *data;
	filerepo::search_callback cb;
};

struct SearchHeader {
	unsigned size;				// Header INCLUDED

	unsigned include_all;		// (new) if contains '+'
	unsigned include_length;	// in bytes

	unsigned char num_include;
	unsigned char num_exclude;

	SearchResponseData response;
};

struct TSBuffer {
	npp::CriticalSection cs;
	std::vector<char> data;
};

struct InputRequest {
	InputRequest(HANDLE wu, TSBuffer &b) : wakeup_event(wu), buffer(b) {}

	HANDLE wakeup_event;
	TSBuffer &buffer;
};

void append_data(TSBuffer &dest, const void *start, unsigned size)
{
	npp::CriticalSectionScope h(dest.cs);
	stream::pack_bytes(dest.data, start, size);
}

void append_input(InputRequest *irs, const void *start, unsigned size) {
	const InputRequest &ir = *irs;
	TSBuffer &buffer = ir.buffer;

	append_data(buffer, start, size);

	::SetEvent(ir.wakeup_event);
}

struct FileRepo {
	FileRepo();
	~FileRepo();

	void start();
	void stop();
	void wait_for_pending_jobs();

	void search(const wchar_t *s, void *userdata, filerepo::search_callback scb);
	void add_directories(Json::Value const &solution);

	void append_inputdata(const void *start, unsigned size); // thread safe/locking

private:
	void run();

	static unsigned int  __stdcall run_tf(void*);

	std::vector<char> _filedata;
	void *_monitor;
	bool _exit_requested;

	HANDLE _wakeup_event;
	npp::Thread *_thread;
	npp::CriticalSection _cs;

	TSBuffer _input_buffer;
	InputRequest *_input_requests;
	std::vector<npp::Thread *> _worker_threads;

	unsigned _outstanding_parsers;
	bool _monitored_directories;
};

void foldermonitor_callback(void *user_data, void *s, unsigned n) {
	DEBUG_PRINT("[foldermonitor_callback data size : %d]", n);
	FileRepo *db = (FileRepo*)user_data;
	db->append_inputdata(s, n);
}


unsigned int __stdcall directory_parse_tf(void*);

struct ThreadParams {
	ThreadParams(InputRequest &ir, bool *s) : input_requests(ir), shutdown(s) {}
	InputRequest &input_requests;
	bool *shutdown;
};

struct DPThreadParams : ThreadParams {
	DPThreadParams(InputRequest &ir, bool *s, String d, String incf, String exlf, bool r) :
	ThreadParams(ir, s), directory(d), inc_filter(incf), exl_filter(exlf), recursive(r) {}

	String directory, inc_filter, exl_filter;
	bool recursive;
};

struct SearchInfo {
	unsigned search_full; // '+'

	unsigned char num_exclude;
	unsigned char num_include;

	std::vector<char> exclude_tokens;
	std::vector<char> include_tokens;
};


void cleanup(String& s)
{
	static const String removeme(L"-");

	String::iterator c(s.begin());
	while(c!=s.end()) {
		if(removeme.find(*c) != std::string::npos) {
			c = s.erase(c);
		} else {
			++c;
		}
	}
}

bool build_searchinformation(const wchar_t *s, SearchInfo &out) {
	const wchar_t null(0);

	unsigned char &num_exclude = out.num_exclude, &num_include = out.num_include;
	num_exclude = 0, num_include = 0;
	unsigned &include_all = out.search_full;
	std::vector<char> &exclude_tokens = out.exclude_tokens;
	std::vector<char> &include_tokens = out.include_tokens;

	include_all = 0;

	String buf;
	std::wstringstream ss(s);

	while (ss >> buf) {
		wchar_t first_char = buf.at(0);
		bool exclude_token = first_char == L'-';
		bool include_all_token = first_char == L'\\';
		include_all = (include_all == 0 ? (include_all_token ? 1 : 0) : 1);

		cleanup(buf);
		unsigned str_len = (unsigned)buf.length();
		if(str_len) {
			if (include_all_token && str_len <= 1)
				continue;

			const wchar_t *b = buf.c_str();
			if (include_all_token) { --str_len; ++b; }

			std::vector<char> &insert_to = (exclude_token ? exclude_tokens : include_tokens);
			unsigned char &add_to = (exclude_token ? num_exclude : num_include);
			npp::stream::pack_bytes(insert_to, b, str_len*sizeof(wchar_t));
			npp::stream::pack(insert_to, null);

			add_to = add_to+1;

		} else {
			DEBUG_PRINT("Skipping empty!");
		}
	}

	bool any = (include_all+num_exclude+num_include) > 0;
	if(!any)
		include_all = 1;

	return true;
}

} // anonymous

namespace filerepo
{
	FileRepositoryHandle allocate_handle()
	{
		FileRepo *repo = new FileRepo();
		repo->start();
		return repo;
	}

	void stop(FileRepositoryHandle &rh)
	{
		if(!rh) {
			DEBUG_PRINT("[filerepo] ERROR : Invalid handle to stop!");
			return;
		}

		FileRepo *repo = (FileRepo *)rh;
		repo->stop();
		rh = 0;
	}

	void add_solution(FileRepositoryHandle rh, Json::Value const& dirs)
	{
		FileRepo *repo = (FileRepo *)rh;
		repo->add_directories(dirs);
	}
	// search string,
	// userdata that will provided as first parameter to 'search_callback'
	void search(FileRepositoryHandle rh, const wchar_t *s, void *ud, search_callback scb)
	{
		FileRepo *repo = (FileRepo *)rh;
		repo->search(s, ud, scb);
	}

} // namespace file_repo

namespace
{

FileRepo::FileRepo() :
_monitor(0),
_exit_requested(false),
_wakeup_event(0),
_thread(0),
_input_requests(0),
_outstanding_parsers(0),
_monitored_directories(false)
{
	folder_monitor::RegisterContext ctx = { this, foldermonitor_callback };

	_monitor = folder_monitor::allocate(&ctx);
}

void FileRepo::append_inputdata(const void *start, unsigned size) {
	append_input(_input_requests, start, size);
}

FileRepo::~FileRepo() {
	npp::thread_stop(_thread);
	npp::thread_destroy(_thread);

	::CloseHandle(_wakeup_event);
	delete _input_requests;
}

void FileRepo::start()
{
	unsigned h = 0;
	stream::pack(_filedata, h);

	const char *eventname = 0;
	BOOL manual_reset = TRUE, initial_state = FALSE;
	_wakeup_event = ::CreateEventA(0, manual_reset, initial_state, eventname);
	_input_requests = new InputRequest(_wakeup_event, _input_buffer);

	_thread = npp::thread_create(FileRepo::run_tf, this);
	npp::thread_start(_thread);
}

void FileRepo::stop()
{
	if(!_thread)
		return; // not started

	folder_monitor::stop(_monitor);

	_exit_requested = true;
	::SetEvent(_wakeup_event);
}

void FileRepo::wait_for_pending_jobs()
{
	std::vector<npp::Thread*>::iterator i(_worker_threads.begin()), end(_worker_threads.end());
	while(i!=end) {
		npp::Thread *t = (*i);

		if(!npp::thread_wait(t, 5000)) {
			std::string err_msg = win_aux::get_last_error();
			DEBUG_PRINT("thread_wait failure. GetLastError(0X%.8X), msg(%s)\n", ::GetLastError(), err_msg.c_str());
		}

		npp::thread_stop(t);
		npp::thread_destroy(t);

		++i;
	}
}

unsigned int  __stdcall FileRepo::run_tf(void* fdb) {
	FileRepo *f = (FileRepo*)fdb;
	f->run();
	DEBUG_PRINT("[FileRepo] Deleting filerepo!");
	f->wait_for_pending_jobs();

	delete f;

	return 0;
}

void FileRepo::run() {
	unsigned num_delayed = 0;

	std::vector<char> delayed_events;
	stream::pack(delayed_events, num_delayed);

	std::vector<char> temp_buffer;
	while(!_exit_requested) {
		::WaitForSingleObject(_wakeup_event, INFINITE);
		if(_exit_requested)
			return;

		DEBUG_PRINT("[Thread] Woken up!");
		TSBuffer &input_buffer = _input_requests->buffer;
		std::vector<char>& indata = input_buffer.data;

		std::vector<char> workbuffer;
		{
			npp::CriticalSectionScope csh(input_buffer.cs);
			if(!indata.empty()) {
				const char *start = &indata[0];
				unsigned size = (unsigned)indata.size();
				stream::pack_bytes(workbuffer, start, size);
			}
			indata.clear();
		}

		unsigned n = 0;
		unsigned size = (unsigned)workbuffer.size();
		DEBUG_PRINT("[filerepo] start working, workbuffersize :  %d bytes", size);
		while(size != n) {
			unsigned debug_size = (unsigned)workbuffer.size();
			(void)debug_size;
			const char *b = &workbuffer[n];
			unsigned consume_n = 0;

			const unsigned &header = stream::unpack<unsigned>(b);
			if(header == filerepo_headers::CHANGE_ADD) {
				DEBUG_PRINT("[Thread] Got change data, adding!");
				const unsigned buffer_size = stream::unpack<unsigned>(b);

				filerepo::aux::merge_dbs(_filedata, b, buffer_size);
				consume_n = sizeof(unsigned)+buffer_size;
			} else if(header == filerepo_headers::CHANGE_REMOVE) {
				DEBUG_PRINT("[Thread] Got change data, SHOULD remove!");
				const unsigned buffer_size = stream::unpack<unsigned>(b);

				filerepo::aux::exclude_db(_filedata, b, buffer_size);
				filerepo::aux::exclude_db(delayed_events, b, buffer_size);

				consume_n = sizeof(unsigned)+buffer_size;
			} else if(header == filerepo_headers::CHANGE_UPDATE) {
				DEBUG_PRINT("[Thread] Got change data, SHOULD update!");

				const unsigned buffer_size = stream::unpack<unsigned>(b);
				filerepo::aux::add_replace_db(delayed_events, b, buffer_size);
				consume_n = sizeof(unsigned)+buffer_size;
			} else if(header == filerepo_headers::QUERY_FILES) {
				DEBUG_PRINT("[Thread] Got search request!");
				const SearchHeader &sh = stream::unpack<SearchHeader>(b);

				const wchar_t *include = (const wchar_t *)b;
				const wchar_t *exlude =  (const wchar_t *)(b+sh.include_length);

				unsigned num_res = filerepo::aux::search_db(temp_buffer,
														_filedata,
														sh.include_all,
														sh.num_include,
														sh.num_exclude,
														include,
														exlude,
														delayed_events);


				//
				// callback
				const SearchResponseData &srd = sh.response;
				srd.cb(srd.data, (void*)&temp_buffer[0], num_res);
				//
				consume_n = sh.size;
			} else if(header == filerepo_headers::PARSER_DONE) {

				_outstanding_parsers -= 1;
				if(!_outstanding_parsers && _monitored_directories) {
					DEBUG_PRINT("[thread] *all* parsers done, starting folder monitoring!");
					folder_monitor::start(_monitor);
				}
			} else if(header == filerepo_headers::DIRECTORIES) {
				const unsigned buffer_size = stream::unpack<unsigned>(b);
				unsigned n_dirs = stream::unpack<unsigned>(b);
				while(n_dirs) {
					const unsigned byte_len = stream::unpack<unsigned>(b);
					const wchar_t *dir = (const wchar_t *)b;

					folder_monitor::add_directory(dir);

					stream::advance(b, byte_len);
					--n_dirs;
				}
				consume_n = buffer_size;
			} else if(header == filerepo_headers::CHANGE_DIRECTORY_RENAME) {
				const unsigned buffer_size = stream::unpack<unsigned>(b);

				const unsigned byte_len_from = stream::unpack<unsigned>(b);
				const wchar_t *from_name = (const wchar_t *)b;

				stream::advance(b, byte_len_from);
				const unsigned byte_len_to = stream::unpack<unsigned>(b);
				const wchar_t *to_name = (const wchar_t *)b;

				filerepo::aux::rename_directory(_filedata, from_name, to_name, temp_buffer);

				consume_n = buffer_size;
			} else {
				unsigned fail_bit = 0;
				fail_bit = 1;
			}

			consume_n += sizeof(unsigned); // header!

			DEBUG_PRINT("[filerepo] consumed %d bytes", consume_n);
			n += consume_n;
		}

		{
			npp::CriticalSectionScope csh(input_buffer.cs);
			if (indata.empty()) {
				DEBUG_PRINT("[Thread] Back to waiting!");
				::ResetEvent(_wakeup_event);
			}
		}

	}
}

void FileRepo::search(const wchar_t *s, void *userdata, filerepo::search_callback scb) {

	std::vector<char> searchdata;

	SearchInfo si;
	if(build_searchinformation(s, si)) {
		stream::pack(searchdata, filerepo_headers::QUERY_FILES);

		const wchar_t null(0);

		const std::vector<char> &it = si.include_tokens;
		const std::vector<char> &et = si.exclude_tokens;
		unsigned include_len = (unsigned)(it.empty() ? 0 : it.size());
		unsigned exclude_len = (unsigned)(et.empty() ? 0 : et.size());

		unsigned char num_include_tokens = si.num_include;
		unsigned char num_exclude_tokens = si.num_exclude;

		unsigned show_all = si.search_full;

		unsigned size = (include_len+exclude_len)+sizeof(SearchHeader);

		SearchHeader h = {	size,
							show_all,
							include_len,
							num_include_tokens,
							num_exclude_tokens,
							{ userdata, scb }
						};
		stream::pack(searchdata, h);
		if(include_len)
			stream::pack_bytes(searchdata, &it[0], (unsigned)it.size());

		if(exclude_len)
			stream::pack_bytes(searchdata, &et[0], (unsigned)et.size());
	}

	if(searchdata.empty())
		return;

	append_input(_input_requests, &searchdata[0], (unsigned)searchdata.size());
}

void FileRepo::add_directories(Json::Value const &solution) {
	Json::Value const &directories = solution["directories"];
	folder_monitor::add_solutions(_monitor, directories);

	unsigned size = directories.size();
	while(size) {
		_outstanding_parsers += 1;

		const Json::Value &dir = directories[--size];
		const char *d = dir["path"].asCString();
		const char *include_filter = (dir["include_filter"].isString() ? dir["include_filter"].asCString() : 0);
		const char *exclude_filter = (dir["exclude_filter"].isString() ? dir["exclude_filter"].asCString() : 0);
		bool recursive = (dir["recursive"].isBool() ? dir["recursive"].asBool() : false);
		bool monitored = (dir["monitored"].isBool() ? dir["monitored"].asBool() : false);

		_monitored_directories = (_monitored_directories ? _monitored_directories : monitored);
		String wd = string_util::to_wide(d);
		String wif = (include_filter ? string_util::to_wide(include_filter) : L"");
		String wef = (exclude_filter ? string_util::to_wide(exclude_filter) : L"");

		DPThreadParams *tp = new DPThreadParams(*_input_requests, &_exit_requested, wd, wif, wef, recursive);
		npp::Thread *t = npp::thread_create(directory_parse_tf, tp);
		_worker_threads.push_back(t);
		npp::thread_start(t);
	}
}

} // anonymous

namespace {

	#include <stack>

	unsigned int  __stdcall directory_parse_tf(void* params) {
		DEBUG_PRINT("[filerepo] Directoryparser thread starting.");
		DPThreadParams *tp = (DPThreadParams*)params;
		bool *shutdown = tp->shutdown;

		unsigned num_records = 0;

		std::vector<char> files;
		stream::pack(files, num_records);

		const String &include_filter = tp->inc_filter;
		const String &exlude_filter = tp->exl_filter;
		const String &start_directory = tp->directory;
		bool recursive = tp->recursive;

		HANDLE hFind = INVALID_HANDLE_VALUE;
		WIN32_FIND_DATA ffd;

		const wchar_t *filter_include = (include_filter.empty() ? 0 : include_filter.c_str());
		const wchar_t *filter_exclude = (exlude_filter.empty() ? 0 : exlude_filter.c_str());

		const bool include_all_files = ((!filter_include && !filter_exclude) ? true : 0);

		String spec;
		std::vector<String> directories;
		std::stack<String> enum_directories;

		enum_directories.push(start_directory);

		while (!enum_directories.empty()) { //
			String path = enum_directories.top();

			spec = path;
			file_util::append_slash(spec);

			directories.push_back(spec);

			spec += L"*.*";

			enum_directories.pop();

			hFind = FindFirstFile(spec.c_str(), &ffd);

			if (hFind == INVALID_HANDLE_VALUE)
				break;

			do {
				bool is_directory = (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
				const wchar_t *fn = ffd.cFileName;

				if(is_directory) {
					wchar_t first_char = *fn;
					bool skip = (first_char == L'.' || first_char == L'$');
					if(skip)
						continue;
				}

				if (is_directory) {
					if (recursive) {
						String pushed = path;
						file_util::append_slash(pushed);
						pushed.append(fn);

						enum_directories.push(pushed);
					}
				} else {
					// is file
					const wchar_t *e = file_util::fileextension(fn, false);
					bool include = include_all_files;
					if (!include_all_files) {
						if(e) {
							include = (filter_include ? string_util::contains_tokens(e, filter_include, false, L'.') : !string_util::contains_tokens(e, filter_exclude, false, L'.'));
						} else {
							include = false;
						}
					}

					if (include) {
						wchar_t datestring[17] = {};
						//const unsigned datestring_len = 16;
						filerepo::make_internal_datestring_ft(datestring, &ffd.ftLastWriteTime);

						String full_filename(path); file_util::append_slash(full_filename);
						full_filename.append(fn);
						const wchar_t *full = full_filename.c_str();

						filerepo::aux::insert_filerecord(files, full, datestring);

						num_records += 1; // increase, store at exit
					}
				} // else
			} while (::FindNextFile(hFind, &ffd) != 0 && !*shutdown);

			if (GetLastError() != ERROR_NO_MORE_FILES) {
				FindClose(hFind);
				//return;
				break;
			}

			FindClose(hFind);
			hFind = INVALID_HANDLE_VALUE;
		} // directories

		*((unsigned*)&files[0]) = num_records; // patch files

		// temp buffer with change info
		std::vector<char> data_buffer;

		unsigned data_header = filerepo_headers::CHANGE_ADD;
		stream::pack(data_buffer, data_header);

		// NOTE :	This works only as we have a "real db" after here (with a unsigned records field)
		//			WAS a change header
		unsigned file_size = (unsigned)files.size();
		stream::pack(data_buffer, file_size);

		data_buffer.insert(data_buffer.end(), files.begin(), files.end());

		// PACK 'SEEN_DIRECTORIES' HERE

		{
			const unsigned sow = sizeof(wchar_t);
			const wchar_t null(0);

			unsigned data_size = 42;
			unsigned header = filerepo_headers::DIRECTORIES;
			stream::pack(data_buffer, header);
			unsigned insert_point = (unsigned)data_buffer.size();
			stream::pack(data_buffer, data_size);

			unsigned num_dirs = (unsigned)directories.size();

			stream::pack(data_buffer, num_dirs);

			for (unsigned i=0;i<num_dirs;++i) {
				const String &d = directories[i];
				unsigned l = (unsigned)d.length();
				stream::pack(data_buffer, (unsigned)((l+1)*sow));

				stream::pack_bytes(data_buffer, (const char *)d.c_str(), l*sow);
				stream::pack(data_buffer, null);
			}

			data_size = (unsigned)data_buffer.size() - insert_point;

			unsigned *patch = (unsigned*)((&data_buffer[0])+insert_point);

			*patch = data_size;

		}
		// end
		unsigned parser_done = filerepo_headers::PARSER_DONE;
		stream::pack(data_buffer, parser_done);

		append_input(&tp->input_requests, &data_buffer[0], (unsigned)data_buffer.size());

		delete tp;

		DEBUG_PRINT("[FileRepo] Parser thread exiting!");
		return 0;
	}

} // namespace anonymous
