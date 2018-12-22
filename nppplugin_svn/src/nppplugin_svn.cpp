#include "nppplugin_svn.h"
#include "npp/plugin/npp_plugin_interface.h"

#include "nppplugin_solutionhub_interface/nppplugin_solutionhub_com_interface.h"
#include "nppplugin_solutionhub_interface/filerecords.h"

#include "debug.h"

#include <string>
#include <vector>

#include <winnls.h>		// MultiByteToWideChar
#include <Shlwapi.h>	// PathFileExists

#include "file/file_system.h"
#include "ini/simple_ini.h"

typedef std::wstring String;

namespace string
{
	String to_wide(const char *s) {
		unsigned L = MultiByteToWideChar(CP_ACP, 0, s, -1, 0, 0);

		std::vector<wchar_t> t(L);
		wchar_t *buf = &t[0];
		MultiByteToWideChar(CP_ACP, 0, s, L, buf, L);

		return String(buf);
	}

	bool ends_with_either(const wchar_t *s, wchar_t a, wchar_t b) {
		if(!s)
			return false;

		unsigned L = (unsigned)wcslen(s);
		if(!L)
			return false;

		unsigned end = L-1;
		return (s[end] == a) || (s[end] == b);
	}

	bool ends_with_slash(const wchar_t *p) {
		return ends_with_either(p, L'\\', L'/');
	}

	void append_slash(String &d) {
		if(!ends_with_slash(d.c_str()))
			d += L"\\";
	}
}

namespace tempbuffer {
	const unsigned NUM_BUFFERS = 1;
	const unsigned BUFFERSIZE = MAX_PATH;

	wchar_t buffer[NUM_BUFFERS][BUFFERSIZE];
}

namespace {
	wchar_t tortoise_process_path[MAX_PATH];
	bool init_success = false;

	String project_source_directory(L"");
	bool project_valid = false;

	void npp_aux_document_get_current_full(wchar_t *out, unsigned out_size)
	{
		::SendMessage(npp_plugin::npp(), NPPM_GETFULLCURRENTPATH, (WPARAM) out_size, (LPARAM)out);
	}

	void ini_parserhandler(void *, const char *key, const char *value)
	{
		if(_stricmp(key, "tortoise_proc_path") == 0)
		{
			String t = string::to_wide(value);
			unsigned L = (unsigned)t.length();

			if(L && MAX_PATH > L) {
				memcpy(tortoise_process_path, t.c_str(), L*sizeof(wchar_t));
				tortoise_process_path[L] = 0;
			}
		}
	}

	void get_tortoise_install_directory_settings()
	{
		using namespace npp;

		const wchar_t *file = npp_plugin::settings_file();

		unsigned fs = file_system::filesize(file);
		if(!fs)
			return;

		char *buffer = new char[fs+1];
		fs = file_system::read(file, buffer, fs);
		if(!fs) {
			delete [] buffer;
			return;
		}

		buffer[fs] = 0;
		simple_ini::parse(buffer, ini_parserhandler, 0);

		delete [] buffer;
	}


	void get_svn_root_directory();
	void do_tortoise_command(const String &com, const String &path);

	String quote(const String &s)
	{
		String res(L"\"");
		res += s;
		res += L"\"";
		return res;
	}

	bool is_ok(bool project_command)
	{
		if(project_command)
			get_svn_root_directory();

		if(!project_valid && project_command) {
			const wchar_t *title =	L"TortoiseSVN plugin - Invalid solution";
			const wchar_t *body =	L"Failed to get 'svn_root_dir' attribute from SolutionHub.\n\n"
									L"Please add 'svn_root_dir' attribute to your connected solution.";

			::MessageBox(npp_plugin::npp(), body, title, MB_OK|MB_ICONERROR);
			return false;
		}

		if(!init_success) {
			const wchar_t *title =	L"TortoiseSVN plugin - Init failed";
			const wchar_t *body =	L"Failed to get install directory of TortoiseSVN.\n\n"
									L"Make sure you have TortoiseSVN installed.\n\n"
									L"Read the documentation(PluginMenu->Tortoise SVN->Help) how to manually point out the executable.\n";

			::MessageBox(npp_plugin::npp(), body, title, MB_OK|MB_ICONERROR);
			return false;
		}

		return true;
	}

	void do_file_command(const wchar_t *command)
	{
		if(!is_ok(false))
			return;

		wchar_t *file = tempbuffer::buffer[0];
		npp_aux_document_get_current_full(file, tempbuffer::BUFFERSIZE);
		if(*file)
			do_tortoise_command(command, quote(file));
	}

	bool get_tortoise_install_directory()
	{
		HKEY key;
		int flag = KEY_READ;
		if(RegOpenKeyEx(HKEY_LOCAL_MACHINE, L"Software\\TortoiseSVN", 0, flag , &key) != ERROR_SUCCESS)
		{
			flag |= KEY_WOW64_64KEY;
			if(RegOpenKeyEx(HKEY_LOCAL_MACHINE, L"Software\\TortoiseSVN", 0, flag , &key) != ERROR_SUCCESS)
				return false;
		}

		DWORD length = MAX_PATH;

		if(RegQueryValueEx(key, L"ProcPath", NULL, NULL, (LPBYTE)tortoise_process_path, &length) != ERROR_SUCCESS)
			return false;

		return true;
	}

	bool launch_tortoise(const String &command)
	{
		String full_command = tortoise_process_path;
		full_command += L" ";
		full_command += command;

		STARTUPINFOW si;
		PROCESS_INFORMATION pi;
		memset(&si, 0, sizeof(si));
		memset(&pi, 0, sizeof(pi));
		si.cb = sizeof(si);

		return CreateProcess(	NULL,
								const_cast<LPWSTR>(full_command.c_str()),
								NULL,
								NULL,
								FALSE,
								CREATE_DEFAULT_ERROR_MODE,
								NULL,
								NULL,
								&si,
								&pi) != 0;
	}

	void get_svn_root_directory()
	{
		char t[256] = {};
		AttributeQuery aq; memset(&aq, 0, sizeof(aq));
		aq.attribute_name = "svn_root_dir";
		aq.result_buffer = t;
		aq.result_buffersize = 256;

		CommunicationInfo comm;
		comm.internalMsg = NPPM_SOLUTIONHUB_QUERY_ATTRIBUTE_HOOKED;
		comm.srcModuleName = npp_plugin::module_name();

		comm.info = &aq;

		::SendMessage(npp_plugin::npp(), NPPM_MSGTOPLUGIN, (WPARAM)SOLUTIONHUB_DLL_FILE_NAME, (LPARAM)&comm);

		if(aq.result == SolutionHubResults::SH_NO_ERROR && *t) {
			// success
			project_source_directory = string::to_wide(t);
			string::append_slash(project_source_directory);
			project_valid = true;
		} else {
			project_valid = false;
		}
	}

	/*
		http://tortoisesvn.net/docs/release/TortoiseSVN_en/tsvn-automation.html

		/closeonend:0 don't close the dialog automatically
		/closeonend:1 auto close if no errors
		/closeonend:2 auto close if no errors and conflicts
		/closeonend:3 auto close if no errors, conflicts and merges
		/closeonend:4 auto close if no errors, conflicts and merges for local operations
	*/
	void do_tortoise_command(const String& com, const String& path)
	{
		static String svn_command(L"/command:");
		static String svn_path(L"/path:");

		String full_command = svn_command;
		full_command += com;
		full_command += L" ";

		full_command += svn_path;
		full_command += path;

		full_command += L" /closeonend:0";

		launch_tortoise(full_command);
	}

	void register_to_solutionhub()
	{
		SolutionHubRegisterContext rc;
		rc.alias = "tsvn";
		rc.mask = NPP_SH_RCMASK_NONE;

		CommunicationInfo comm;
		comm.internalMsg = NPPM_SOLUTIONHUB_HOOK_RECEIVER;
		comm.srcModuleName = npp_plugin::module_name();
		comm.info = &rc;

		::SendMessage(npp_plugin::npp(), NPPM_MSGTOPLUGIN, (WPARAM)SOLUTIONHUB_DLL_FILE_NAME, (LPARAM)&comm);
	}
}

namespace npp_plugin_svn
{
	void init()
	{
		memset(tortoise_process_path, 0, sizeof(tortoise_process_path));

		init_success = get_tortoise_install_directory();
		if(!init_success) {
			get_tortoise_install_directory_settings();

			init_success = *tortoise_process_path && ::PathFileExists(tortoise_process_path);
		}

		register_to_solutionhub();
	}

	void terminate() {}

	void project_commit()
	{
		if(!is_ok(true))
			return;

		do_tortoise_command(L"commit", quote(project_source_directory));
	}

	void project_update()
	{
		if(!is_ok(true))
			return;

		do_tortoise_command(L"update", quote(project_source_directory));
	}

	void project_log()
	{
		if(!is_ok(true))
			return;

		do_tortoise_command(L"log", quote(project_source_directory));
	}

	//! File commands
	void file_diff() { do_file_command(L"diff"); }

	void file_log() { do_file_command(L"log"); }

	void file_commit() { do_file_command(L"commit"); }
	void file_update() { do_file_command(L"update"); }

	void file_conflict_editor() { do_file_command(L"conflicteditor"); }

	void file_add() { do_file_command(L"add"); }

	void file_revert() { do_file_command(L"revert"); }

	void file_resolve() { do_file_command(L"resolve"); }

	void file_remove() { do_file_command(L"remove"); }

	void file_rename() { do_file_command(L"rename"); }

	void file_lock() { do_file_command(L"lock"); }

	void file_unlock() { do_file_command(L"unlock"); }

	void file_blame() { do_file_command(L"blame"); }

	void open_helpfile()
	{
		const wchar_t *file = npp_plugin::help_file();

		::SendMessage(npp_plugin::npp(), NPPM_DOOPEN, 0, (LPARAM)file);
	}

	void open_configfile()
	{
		const wchar_t *file = npp_plugin::settings_file();

		::SendMessage(npp_plugin::npp(), NPPM_DOOPEN, 0, (LPARAM)file);
	}

#ifdef DEMONSTRATE_SH_SEARCH

	template<typename T, int STACKSIZE>
	struct scoped_buffer {
		explicit scoped_buffer() : buff(0) {
		}
		scoped_buffer(unsigned n) : buff(0) {
			allocate(n);
		}
		~scoped_buffer() {
			if (buff && (buff != stack))
				delete[] buff;
		}
		void allocate(unsigned n) {
			if (buff != 0)
				*((int*)0) = STACKSIZE;

			if (STACKSIZE >= n)
				buff = stack;
			else
				buff = new T[n];
		}
		T *ptr() { return buff; }
		operator T*() { return buff; }

	private:
		T stack[STACKSIZE];
		T *buff;
		scoped_buffer& operator= (const scoped_buffer&);
	};

	std::wstring npp_aux_get_selected_text() {
		unsigned N = ::SendMessage(npp_plugin::scintilla_view_current(), SCI_GETSELTEXT, 0 , 0);

		if (0 >= N)
			return L"";

		scoped_buffer<char, 1024> scoped(N);
		char *c = scoped;

		::SendMessage(npp_plugin::scintilla_view_current(), SCI_GETSELTEXT, 0, (LPARAM)c);

		if (!*c)
			return L"";

		return string::to_wide(c);
	}

	void temp_search()
	{
		String s = npp_aux_get_selected_text();
		SearchRequest sr; memset(&sr, 0, sizeof(sr));
		sr.result_notification = 42;
		sr.searchstring = s.c_str();

		CommunicationInfo comm;
		comm.internalMsg = NPPM_SOLUTIONHUB_SEARCH_SOLUTION;
		comm.srcModuleName = npp_plugin::module_name();
		comm.info = &sr;

		::SendMessage(npp_plugin::npp(), NPPM_MSGTOPLUGIN, (WPARAM)SOLUTIONHUB_DLL_FILE_NAME, (LPARAM)&comm);
	}

	void temp_on_searchresponse(void *data)
	{
		char TEMPBUFFER[4096];

		SearchResponse &sr = *((SearchResponse*)data);
		const char *buffer = (const char *)sr.data;

		FileRecords frs(buffer);
		unsigned max_records = min(10, frs.num_records);

		int N = sprintf(TEMPBUFFER, "Search result : num records(%u)", frs.num_records);
		if (N>0)
			::SendMessage(npp_plugin::scintilla_view_current(), SCI_ADDTEXT, N , (WPARAM)TEMPBUFFER);

		for(unsigned i=0; i < max_records;++i) {
			FileRecord fr = frs.filerecord(i);
			//DEBUG_PRINT("[tsvn] fn(%S), p(%S), d(%S)", fr.filename, fr.path, fr.date);
			N = sprintf(TEMPBUFFER, "filename(%S), path(%S), date(%S)\n", fr.filename, fr.path, fr.date);
			if (N > 0)
				::SendMessage(npp_plugin::scintilla_view_current(), SCI_ADDTEXT, N, (WPARAM)TEMPBUFFER);
		}

		//DEBUG_PRINT("[tsvn] num records(%u)", frs.num_records);
	}
#endif
}
