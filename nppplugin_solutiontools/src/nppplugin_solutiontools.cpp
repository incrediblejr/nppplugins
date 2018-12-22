#include "nppplugin_solutiontools.h"
#include "nppplugin_solutiontools_messages.h"

#include "npp/plugin/npp_plugin_interface.h"

#include "nppplugin_solutionhub_interface/nppplugin_solutionhub_com_interface.h"
#include "nppplugin_solutionhub_interface/filerecords.h"

#include "stream.h"

#include "string/string_utils.h"
#include "debug.h"
#include "json_aux/json_aux.h"

#include <wctype.h> // towlower

#include <winnls.h> // MultiByteToWideChar
#include <Shlwapi.h> // Path stuff

#include <map>
#include <vector>
#include <algorithm>

typedef std::wstring String;

#if !defined(min) && !defined(max)
	#define min(x,y) ((a) < (b) ? (a) : (b))
	#define max(x,y) ((a) > (b) ? (a) : (b))
#endif

namespace {
	const unsigned HEADER_SWITCH = 0;
	const unsigned HEADER_SEARCH = 1;
	const unsigned HEADER_GOTO_FILELINE = 2;

	void nppaux_document_open(const wchar_t *full, int line)
	{
		LRESULT open_res = ::SendMessage(npp_plugin::npp(), NPPM_DOOPEN, 0, (LPARAM)full);
		if (open_res == 0)
			open_res = 0;

		if(line != -1)
		{
			::SendMessage(npp_plugin::scintilla_view_current(), SCI_ENSUREVISIBLE, (WPARAM)line, 0);
			::SendMessage(npp_plugin::scintilla_view_current(), SCI_GOTOLINE, (WPARAM)line, 0);

			int first_visible_line = (int)::SendMessage(npp_plugin::scintilla_view_current(), SCI_GETFIRSTVISIBLELINE, 0, 0);
			int num_visible_lines = (int)::SendMessage(npp_plugin::scintilla_view_current(), SCI_LINESONSCREEN, 0, 0);
			int last_visible_line = first_visible_line+num_visible_lines;

			int lines_to_scroll = 0;
			if(first_visible_line >= line)
			{
				lines_to_scroll = line - first_visible_line;
				lines_to_scroll -= num_visible_lines/2;
			}
			else if(last_visible_line >= line)
			{
				lines_to_scroll = line - last_visible_line;
				lines_to_scroll += num_visible_lines/2;
			}

			::SendMessage(npp_plugin::scintilla_view_current(), SCI_LINESCROLL, 0, (LPARAM)lines_to_scroll);
		}

		::PostMessage(npp_plugin::npp(), WM_ACTIVATE, 0, 0);
	}
}

namespace {
	std::map<String, std::vector<char> > switch_map;
	bool switch_file_ofis_disabled;
}

namespace {
	void str_replace(char *s, char a, char b) {
		while(*s) {
			if(*s == a)
				*s = b;

			++s;
		}
	}

	void str_replace_wide(wchar_t *s, wchar_t a, wchar_t b) {
		while(*s) {
			if(*s == a)
				*s = b;

			++s;
		}
	}

	char *stb_skipwhite(char *s) {
		while (isspace((unsigned char) *s)) ++s;
		return s;
	}

	char *stb_skipnewline(char *s) {
		if (s[0] == '\r' || s[0] == '\n') {
			if (s[0]+s[1] == '\r' + '\n') ++s;
			++s;
		}
		return s;
	}

	char *stb_trimwhite(char *s) {
		int i,n;
		s = stb_skipwhite(s);
		n = (int) strlen(s);
		for (i=n-1; i >= 0; --i)
			if (!isspace(s[i]))
				break;
		s[i+1] = 0;
		return s;
	}
}

namespace
{
	bool npp_open_file(const wchar_t *full_filename)
	{
		return (1 == ::SendMessage(npp_plugin::npp(), NPPM_DOOPEN, 0, (LPARAM)full_filename));
	}
}

namespace settings
{
	// extension, include 'start' (i.e. require, #include, ...)
	std::map<String, std::vector<char> > language_include_map;

	std::vector<std::string> non_open_close;
	unsigned non_open_close_n;

	std::string open_close_tags_str, open_chars_str;
	const char *open_close_tags, *open_chars;

	bool check_open_tag;

	bool mouse_enabled;
	LONG mouse_diff;

	void set_default()
	{
		check_open_tag = false;

		language_include_map.clear();

		non_open_close.clear();
		non_open_close_n = 0;

		open_chars_str = "\"'";
		open_close_tags_str = "<>";

		open_chars = open_chars_str.c_str();
		open_close_tags = open_close_tags_str.c_str();

		//! Mouse
		mouse_enabled = true;
		mouse_diff = 4;

		//! Switch map
		switch_map.clear();
		switch_file_ofis_disabled = false;
	}

	void load()
	{
		using namespace npp;

		Json::Value config;
		if(json_aux::json_from_file(npp_plugin::settings_file(), config))
		{
			check_open_tag = (config["check_open_tag"].isBool() ? config["check_open_tag"].asBool() : check_open_tag);

			if(config["open_close_tags"].isString())
			{
				open_close_tags_str = config["open_close_tags"].asString();
				open_close_tags = open_close_tags_str.c_str();
			}

			if(config["open_chars"].isString())
			{
				open_chars_str = config["open_chars"].asString();
				open_chars = open_chars_str.c_str();
			}

			if(config["non_open_close"].isArray()) {
				unsigned S = config["non_open_close"].size();
				for(unsigned i=0; i < S;++i) {
					if(config["non_open_close"][i].isString())
						non_open_close.push_back(config["non_open_close"][i].asString());
				}

				non_open_close_n = (unsigned)non_open_close.size();
			}

			//! Mouse diff
			if(config["mouse_diff"].isNumeric())
				mouse_diff = (LONG) config["mouse_diff"].asDouble();

			if(config["mouse_enabled"].isBool())
				mouse_enabled = config["mouse_enabled"].asBool();

			if(config["switch_file_disable_ofis"].isBool())
				switch_file_ofis_disabled = config["switch_file_disable_ofis"].asBool();

			//! Language include

			if(config["language_include"].isObject()) {
				const Json::Value json_li = config["language_include"];

				std::vector<std::string> members = json_li.getMemberNames();
				for(unsigned i=0, n = (unsigned)members.size(); i != n; ++i) {
					const std::string extension = members[i];

					const Json::Value &def = json_li[extension];
					if(def.isArray() && !def.empty()) {
						unsigned N = def.size();
						std::vector<char> &si_data = language_include_map[string_util::to_wide(extension.c_str())];

						stream::pack(si_data, N);

						for(unsigned j = 0; j < N; ++j)
						{
							const char *include_tag = def[j].asCString();

							stream::pack_string(si_data, include_tag);
						}
					}
				}
			}

			//! Switch info
			if(config["switch_info"].isObject()) {
				const Json::Value json_si = config["switch_info"];

				std::vector<std::string> members = json_si.getMemberNames();
				for(unsigned i=0; i<members.size(); ++i) {
					const std::string extension = members[i];

					const Json::Value &def = json_si[extension];
					if(def.isArray() && !def.empty()) {
						unsigned N = def.size();
						std::vector<char> &si_data = switch_map[string_util::to_wide(extension.c_str())];

						stream::pack(si_data, N);

						for(unsigned j = 0; j < N; ++j)
						{
							const String switch_extension = string_util::to_wide(def[j].asCString());
							const wchar_t *se = switch_extension.c_str();
							stream::pack_string_wide(si_data, se);

							DEBUG_PRINT("SI %s:%S", extension.c_str(), se);
						}
					}
				}
			}
		}
	}

	void init()
	{
		set_default();
		load();
	}
}

namespace tempbuffer {
	const unsigned NUM_BUFFERS = 2;
	const unsigned BUFFERSIZE = MAX_PATH;
	const unsigned NUM_WIDE_BUFFERS = 4;

	char buffer[NUM_BUFFERS][BUFFERSIZE];
	wchar_t widebuffer[NUM_WIDE_BUFFERS][BUFFERSIZE];
}

namespace {

	LONG diff(LONG a, LONG b)
	{
		LONG l = max(a, b);
		LONG s = min(a, b);
		return (l-s);
	}

	bool key_down(int k) { return (::GetKeyState(k) & 0x80) == 0x80; }

	bool ctrl_down() { return key_down(VK_CONTROL); }
	bool shift_down() { return key_down(VK_SHIFT); }
	bool lalt_down() { return key_down(VK_LMENU); }
	bool ralt_down() { return key_down(VK_RMENU); }

	bool is_nppscintilla(HWND h)
	{
		return (h == npp_plugin::scintilla_view_main() || h == npp_plugin::scintilla_view_secondary());
	}

	HHOOK hook_mouse;
#if 0
	HHOOK hook_wndret;

	LRESULT CALLBACK hook_wndretproc(int nCode, WPARAM wParam, LPARAM lParam)
	{
		if (nCode >= HC_ACTION)
		{
			CWPRETSTRUCT *rs = ((CWPRETSTRUCT*)lParam);

			if ((rs->message == NPPM_MSGTOPLUGIN))
			{
				DEBUG_PRINT("NPPM_MSGTOPLUGIN");
			}
		}

		return ::CallNextHookEx(0, nCode, wParam, lParam);
	}
#endif
	/*	Reference :
	 *
	 *	IF using 'WH_MOUSE_LL' type hook then HWND is always 0
	 *	To get the HWND, ::WindowFromPoint(pt);
	 */

	LRESULT CALLBACK hook_mouseproc(int nCode, WPARAM wParam, LPARAM lParam)
	{
		struct goto_mouse_helper { HWND h; LONG y; };

		static goto_mouse_helper helper = { 0, 0 };

		if(nCode >= 0)
		{
			MOUSEHOOKSTRUCT *hs = (MOUSEHOOKSTRUCT *)lParam;
			HWND h = hs->hwnd;

			LONG py = hs->pt.y;

			switch (wParam)
			{
			case WM_MOUSEMOVE:
			case WM_NCMOUSEMOVE:
				break;
			case WM_LBUTTONDOWN:
			case WM_LBUTTONDBLCLK:
				{
					if(lalt_down()) {
						helper.h = h;
						helper.y = py;
					}
				}
				break;
			case WM_LBUTTONUP:
			case WM_NCLBUTTONUP:
				{
					if((h == helper.h) && is_nppscintilla(h))
					{
						LONG d = diff(helper.y, py);
						DEBUG_PRINT("DIFF : %d, helper y(%d), this y(%d)", d, helper.y, py);

						if(settings::mouse_diff >= d)
							npp_plugin_st::goto_file();
					}
				}
				// intentional fall through
			case WM_RBUTTONDOWN:
			case WM_RBUTTONDBLCLK:
				{
					memset(&helper, 0, sizeof(helper));
				}

			default:
				break;
			}
		}

		return ::CallNextHookEx(hook_mouse, nCode, wParam, lParam);
	}

	void init_hooking()
	{
		hook_mouse = 0;
#if 0
		hook_wndret = 0;
		hook_wndret = ::SetWindowsHookEx(WH_CALLWNDPROCRET, (HOOKPROC)hook_wndretproc, 0, ::GetCurrentThreadId());
		if(!hook_wndret)
			DEBUG_PRINT("WINDOW HOOK FAILED!");
#endif

	}

	void shutdown_hooking()
	{
#if 0
		::UnhookWindowsHookEx(hook_wndret);
		hook_wndret = 0;
#endif
		::UnhookWindowsHookEx(hook_mouse);
		hook_mouse = 0;

	}

	void set_hooking()
	{
		if(settings::mouse_enabled)
		{
			if(!hook_mouse)
			{
				HINSTANCE hinst	= (HINSTANCE)npp_plugin::module();
				hook_mouse = ::SetWindowsHookEx(WH_MOUSE, (HOOKPROC)hook_mouseproc, hinst, 0);

				if(!hook_mouse)
				{
					const char *body = "Failed to hook mouse, please disable it in the settings!";
					const char *title = "SolutionTools";

					::MessageBoxA(npp_plugin::npp(), body, title, MB_OK | MB_ICONERROR);
				}
			}
		}
		else
		{
			if(hook_mouse)
				shutdown_hooking();
		}
	}
}

namespace {
	void npp_aux_document_get_current_full(wchar_t *out, unsigned out_size)
	{
		::SendMessage(npp_plugin::npp(), NPPM_GETFULLCURRENTPATH, (WPARAM) out_size, (LPARAM)out);
	}

	void npp_aux_document_get_current_extension(wchar_t *out, unsigned out_size)
	{
		::SendMessage(npp_plugin::npp(), NPPM_GETEXTPART, (WPARAM) out_size, (LPARAM)out);
	}

	void search(const wchar_t *search_name, void *userdata, unsigned userdata_size)
	{
		SearchRequest sr; memset(&sr, 0, sizeof(sr));
		sr.result_notification = NPPM_ST_ON_SEARCH_RESPONSE;
		sr.searchstring = search_name;
		sr.userdata = userdata;
		sr.userdata_size = userdata_size;

		CommunicationInfo comm;
		comm.internalMsg = NPPM_SOLUTIONHUB_SEARCH_SOLUTION;
		comm.srcModuleName = npp_plugin::module_name();
		comm.info = &sr;

		::SendMessage(npp_plugin::npp(), NPPM_MSGTOPLUGIN, (WPARAM)SOLUTIONHUB_DLL_FILE_NAME, (LPARAM)&comm);
	}

	void sh_search_for(const wchar_t *search_name)
	{
		std::vector<char> search_data;

		npp::stream::pack(search_data, HEADER_SEARCH);
		npp::stream::pack_string_wide(search_data, search_name);
		search(search_name, &search_data[0], (unsigned)search_data.size()); // send to SolutionHub
	}

	String path(const wchar_t *p, bool keep_sep)
	{
		String path_str(p);
		size_t fs = path_str.rfind('\\');
		if(fs == String::npos)
			return path_str;

		size_t L = path_str.length();

		size_t end;
		if(fs == L)	{
			if(!keep_sep)
				end = (fs > 0 ? fs-1 : fs);
			else
				end = fs;
		} else {
			if(!keep_sep)
				end = fs;
			else
				end = (L > fs ? fs+1 : fs);

		}
		return path_str.substr(0, end);
	}

	void register_to_solutionhub()
	{
		SolutionHubRegisterContext rc;
		rc.alias = "ofis";
		rc.mask = NPP_SH_RCMASK_ATTACH;

		CommunicationInfo comm;
		comm.internalMsg = NPPM_SOLUTIONHUB_HOOK_RECEIVER;
		comm.srcModuleName = npp_plugin::module_name();
		comm.info = &rc;

		::SendMessage(npp_plugin::npp(), NPPM_MSGTOPLUGIN, (WPARAM)SOLUTIONHUB_DLL_FILE_NAME, (LPARAM)&comm);
	}

	void process_tag(char *tag)
	{
		const unsigned sow = sizeof(wchar_t);

		str_replace(tag, '/', '\\');

		String wfiletag = string_util::to_wide(tag);
		const wchar_t *fn = file_util::filename(wfiletag.c_str());

		const wchar_t *tag_ext = 0;
		if(fn)
			tag_ext = file_util::fileextension(fn, false);

		wchar_t *B = tempbuffer::widebuffer[0];
		*B = 0;

		npp_aux_document_get_current_full(B, tempbuffer::BUFFERSIZE);

		if(*B) {
			String current(B);
			replace(current.begin(), current.end(), '/', '\\');

			const wchar_t *current_ext = file_util::fileextension(current.c_str(), true); // keep dot
			String current_path = path(current.c_str(), true); // keep last slash

			String check_path = current_path;
			check_path += wfiletag;

			BOOL res = ::PathCanonicalize(B, check_path.c_str());

			DEBUG_PRINT("CHECK : res(%d), value(%S)", res, B);
			if(res) {
				bool handled = false;
				if(::PathFileExists(B)) {
					handled = true;
					npp_open_file(B);
				} else {
					if(!tag_ext && current_ext) {
						unsigned BL = (unsigned)wcslen(B);
						unsigned EXTL = (unsigned)wcslen(current_ext);
						if(tempbuffer::BUFFERSIZE > BL+EXTL) {
							wcscat(B, current_ext);

							if(::PathFileExists(B)) {
								handled = true;
								npp_open_file(B);
							}
						}
					}
				}

				if(!handled) {
					if(tempbuffer::BUFFERSIZE > wfiletag.length())
					{
						wcscpy(B, wfiletag.c_str());
						wchar_t *folder_sep = wcschr(B, L'\\');
						if(folder_sep)
						{
							unsigned L = (unsigned)(folder_sep-B);
							if(3 > L && *B == L'.')
							{
								if(L == 1 || (L == 2 && B[1] == L'.'))
								{
									B = (*(folder_sep+1) ? folder_sep+1 : folder_sep);

									while((folder_sep = wcschr(B, L'\\')) != 0)
									{
										L = (unsigned)(folder_sep-B);
										bool bail = (L != 2 ? true : false);

										if(!bail && L == 2)
										{
											if(!(B[0] == '.' && B[1] == '.'))
												bail = true;
										}
										if(bail)
											break;

										B = (*(folder_sep+1) ? folder_sep+1 : folder_sep);
									}
									B = B-1;
								}
							}

							if(*B != L'\\')
							{
								L = (unsigned)wcslen(B);
								memmove(B+1, B, (L+1)*sow); // move null terminator also
								*B = L'\\';
							}
						}

						sh_search_for(B);
					}
				}
			}
		}
	}

	const char *first_open_char(char *s)
	{
		while(*s) {
			char c = *s;
			if(strchr(settings::open_chars, c))
				return s;

			++s;
		}

		return 0;
	}

	void decode_currentline(char *line)
	{
		const char *start = 0, *end = 0;
		char close_tag = 0;

		const char *s = line;
		while(*s && !end) {
			char c = *s;
			if(start) {
				// we are looking for the close_tag
				if(c == close_tag)
					end = s;
			} else {
				// we have not 'started' i.e. found parse point
				if(strchr(settings::open_chars, c)) {
					// we what the close tag will be
					start = s+1;
					close_tag = c;
				} else {
					const char *ct = strchr(settings::open_close_tags, c);
					if(ct) {
						close_tag = *(ct+1);
						if(close_tag) {
							// not at end.. we have failed miserable then

							start = s+1;
						}
					}
				}
			}
			++s;
		}

		if(start && end) {
			unsigned L = (unsigned)(end-start);
			if(L) {
				memmove(line, start, L);
				line[L] = 0;

				process_tag(line);
			}
		}
	}
}

#include "file/file_system.h"
#include "pugixml/pugixml.hpp"

namespace {

	std::vector<char> autocomplete_buffer;
	bool tooltips_enabled = false;

	void autocomplete_files(std::vector<String> &files, const String &root_path)
	{
		String search_path(root_path);
		search_path.append(L"*.xml");

		WIN32_FIND_DATAW fd;
		const wchar_t *p = search_path.c_str();
		HANDLE h = ::FindFirstFile(p, &fd);
		do
		{
			String full_filename(root_path);
			full_filename += fd.cFileName;
			files.push_back(full_filename);
		} while (::FindNextFile(h, &fd));

		::FindClose(h);
	}

	bool parse_xml(const String &filename, unsigned &num_keywords);

	void parse_autocomplete_files(const std::vector<String> &files)
	{
		(void)files;
		return;
	}

	void init_calltip()
	{
		unsigned dwelltime_ms = 500;

		::SendMessage(npp_plugin::scintilla_view_main(), SCI_SETMOUSEDWELLTIME, (WPARAM)dwelltime_ms, 0);
		::SendMessage(npp_plugin::scintilla_view_secondary(), SCI_SETMOUSEDWELLTIME, (WPARAM)dwelltime_ms, 0);

		String apipath;
		apipath = npp_plugin::module_path();
		apipath += L"\\apis\\";
		std::vector<String> xml_files;
		autocomplete_files(xml_files, apipath);
		parse_autocomplete_files(xml_files);
		String filetoparse = L"gmod lua.xml";

		String final = apipath+filetoparse;
		unsigned num_keywords;

		bool parse_res = parse_xml(final, num_keywords);
		if(!parse_res) {
			tooltips_enabled = false;
		} else if(num_keywords == 0) {
			tooltips_enabled = false;

			char T[1024] = {};
			const char *title =	"Solutiontools (R. " REVISION_HASH ")";
			sprintf(T, "File %S does not contain any keywords.\n\n", final.c_str());

			::MessageBoxA(npp_plugin::npp(), T, title, MB_OK | MB_ICONERROR);
		} else {
			tooltips_enabled = true;
		}
	}

	bool parse_xml(const String &filename, unsigned &num_keywords)
	{
		using namespace npp;
		using namespace pugi;

		num_keywords = 0;

		const wchar_t *filepath = filename.c_str();
		unsigned size = file_system::filesize(filepath);
		if(!size)
			return false;

		std::vector<char> buffer(size);
		file_system::read(filepath, &buffer[0], size);

		xml_document doc;
		xml_parse_result xmlres = doc.load(&buffer[0], pugi::parse_minimal);

		if(!xmlres)
			return false;

		xml_node autocomplete = doc.child("NotepadPlus").child("AutoComplete");
		if(!autocomplete)
			return false;

		stream::pack(autocomplete_buffer, num_keywords);

		std::string temp_buffer;
		for (pugi::xml_node keyword = autocomplete.child("KeyWord"); keyword; keyword = keyword.next_sibling("KeyWord"))
		{
			const char *name = keyword.attribute("name").value();

			const xml_node overload = keyword.child("Overload");
			if(!overload)
				continue;

			num_keywords = num_keywords+1;
			temp_buffer = "";

			unsigned patch_point = (unsigned)autocomplete_buffer.size();

			stream::pack(autocomplete_buffer, (unsigned)0); // recordsize

			//! first pack name (to compare with)
			stream::pack_string(autocomplete_buffer, name);

			const char *returnval = overload.attribute("retVal").value();
			temp_buffer += returnval;
			temp_buffer += " ";
			temp_buffer += name;
			temp_buffer += "(";

			unsigned count = 0;
			for (xml_node param = overload.child("Param"); param; param = param.next_sibling("Param"))
				count += 1;

			if(count)
			{
				for (xml_node param = overload.child("Param"); param; param = param.next_sibling("Param"))
				{
					count = (count ? count-1 : 0);
					const char *pname = param.attribute("name").value();
					temp_buffer += pname;
					if(count)
						temp_buffer += ", ";
				}
			}
			temp_buffer += ")\n";

			const char *description = overload.attribute("descr").value();
			temp_buffer += description;

			unsigned len = (unsigned)temp_buffer.length();
			stream::pack_bytes(autocomplete_buffer, temp_buffer.c_str(), len);
			stream::pack(autocomplete_buffer, (char)0);

			unsigned finished_point = (unsigned)autocomplete_buffer.size();
			unsigned record_size = finished_point-patch_point;
			unsigned *patch = (unsigned*)((&autocomplete_buffer[0])+patch_point);
			*patch = record_size;
		}

		unsigned *patch = (unsigned*)&autocomplete_buffer[0];
		*patch = num_keywords;

		return true;
	}

	bool show_calltip(HWND h, int where, const char *what)
	{
		using namespace npp;
		const unsigned so_u = sizeof(unsigned);

		//! Note : as we pack our string to count the null-terminator!
		unsigned what_len = (unsigned)strlen(what)+1;

		const char *display = 0;
		const char *b = &autocomplete_buffer[0];
		unsigned num_keywords = stream::unpack<unsigned>(b);

		for(unsigned i=0; i != num_keywords; ++i)
		{
			unsigned record_size = stream::unpack<unsigned>(b);
			unsigned str_len = stream::unpack<unsigned>(b);
			const char *compare_str = b;
			const char *desc = b+str_len;
			if(str_len == what_len)
			{
				if(_stricmp(what, compare_str) == 0)
				{
					display = desc;
					break;
				}
			}
			stream::advance(b, record_size-so_u*2);
		}

		if(!display)
			return false;

		::SendMessage(h, SCI_CALLTIPCANCEL, 0, 0);
		::SendMessage(h, SCI_CALLTIPSHOW, where, (LPARAM)display);

		return true;
	}

	bool is_basic_word(int ch) {
		return (ch >= 'A' && ch <= 'Z' ||
				ch >= 'a' && ch <= 'z' ||
				ch == '_' ||
				ch == '.' ||
				ch >= '0' && ch <= '9');
	};

	void dwell_start(HWND h, int position)
	{
		if(!tooltips_enabled)
			return;

		if(0 > position)
			return;

		int line = (int)::SendMessage(h, SCI_LINEFROMPOSITION, position, 0);
		int startpos = (int)::SendMessage(h, SCI_POSITIONFROMLINE, line, 0);
		int endpos = (int)::SendMessage(h, SCI_GETLINEENDPOSITION, line, 0);
		int len = endpos - startpos;
		int offset = position - startpos;

		if((len >= tempbuffer::BUFFERSIZE))
			return;

		char *buffer = tempbuffer::buffer[0];
		len = (int)::SendMessage(h, SCI_GETLINE, line, (LPARAM)buffer);
		buffer[len] = 0;

		int start_offset = offset;

		if(!is_basic_word(buffer[start_offset]))
		{
			//! start searching forward
			start_offset = min(start_offset, len);

			bool started = false;
			while(start_offset != len)
			{
				if(!is_basic_word(buffer[start_offset]))
				{
					if(started)
					{
						start_offset = start_offset;
						break;
					}
					else
						started = true;
				}
				else if(started)
						break;
				++start_offset;
			}
		}
		else
		{
			while(start_offset)
			{
				if(!is_basic_word(buffer[start_offset]))
				{
					break;
				}
				--start_offset;
			}

			if(start_offset != offset)
			{
				bool isbw = is_basic_word(buffer[start_offset]);
				start_offset = (isbw ? start_offset : start_offset+1);
			}
		}

		const char *start = buffer+start_offset;

		int end_offset = max(offset, start_offset);
		while(end_offset != len)
		{
			if(!is_basic_word(buffer[end_offset]))
				break;

			++end_offset;
		}

		char *end = buffer+end_offset;

		unsigned search_len = (unsigned)(end-start);
		if(!search_len)
			return;

		char *tbuffer = tempbuffer::buffer[1];
		memcpy(tbuffer, start, search_len);

		tbuffer[search_len] = 0;

		int tooltip_pos = end_offset+startpos; //! looks weird
		//tooltip_pos = position; //! alternative

		bool res = show_calltip(h, tooltip_pos, tempbuffer::buffer[1]);
#ifdef _DEBUG
		if(!res)
		{
			char T[1024] = {0};
			sprintf(T, "No tooltip for(%s) found!\nLine(%d)\nStartpos(%d)\nEndpos(%d)",
						tempbuffer::buffer[1], line, startpos, endpos);

			::SendMessage(h, SCI_CALLTIPSHOW, tooltip_pos, (LPARAM)T);
		}
#else
		(void)res;
#endif
	}

	void dwell_end(HWND h, int /*position*/)
	{
		if(!tooltips_enabled)
			return;

		::SendMessage(h, SCI_CALLTIPCANCEL, 0, 0);
	}
}

#define IS_NUMBER(x) ((x) >= '0' && (x) <= '9')

const char *allowed_chars = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ._/";

static int is_allowed(int c) { return strchr(allowed_chars, c) != 0; }

static int goto_file_line_decode(const char *s, String &res, int *line)
{
	int l = (int)strlen(s);
	int found_number = 0;
	int found_string_end = -1;
	int was_number = 0;
	int numbers_end_index = -1;
	int last_allowed_char = -1;

	while (l--) {
		int is_number = IS_NUMBER(s[l]);
		if (is_number && (numbers_end_index == -1)) {
			numbers_end_index = l;
		}

		if (was_number && !is_number) {
			char b[64] = {0};
			memcpy(b, s+l+1, (numbers_end_index-l));
			*line = atoi(b);
			numbers_end_index = -1;
			found_number = 1;
		}

		was_number = is_number;

		if (found_number && (found_string_end == -1) && is_allowed(s[l])) {
			found_string_end = l;
			last_allowed_char = s[l];
		} else if (found_string_end != -1) {
			int break_here = 0;
			int c = s[l];

			if (is_allowed(c)) {
				if (last_allowed_char == '.' && c == '.') {
					// probably off by one here :)
					break_here = 1;
					l += 1;
				}
			} else {
				break_here = 1;
			}

			if ( break_here ) {
				unsigned len = (found_string_end-l);
				if (!len)
					return 0;
				else {
					char *B = 0;
					if (len > tempbuffer::BUFFERSIZE-1) {
						B = new char[len+1];
					} else {
						B = tempbuffer::buffer[0];
					}

					memcpy(B, s+l+1, len);
					for (unsigned i=0; i!=len; ++i) {
						if (B[i] == '/')
							B[i] = ' '; //'\\';
						else {
							B[i] = (char)tolower(B[i]);
						}
					}

					B[len] = 0;

					res = string_util::to_wide(B);
					if (B != tempbuffer::buffer[0])
						delete [] B;

					return 1;
				}
			}

			last_allowed_char = c;
		}
	}

	return 0;
}

namespace {
	static void decode_and_goto(const char *s)
	{
		if (!(*s))
			return;

		String r;
		int line;
		if (goto_file_line_decode(s, r, &line)) {
			--line;

			std::vector<char> search_data;
			npp::stream::pack(search_data, HEADER_GOTO_FILELINE);
			npp::stream::pack_string_wide(search_data, r.c_str());
			npp::stream::pack_bytes(search_data, &line, sizeof line);

			search(r.c_str(), &search_data[0], (unsigned)search_data.size()); // send to SolutionHub
		}
	}
}

namespace npp_plugin_st {

	void on_dwellstart(int position, void *hwnd_from)
	{
		HWND h = (HWND)hwnd_from;
		dwell_start(h, position);
	}

	void on_dwellend(int position, void *hwnd_from)
	{
		HWND h = (HWND)hwnd_from;
		dwell_end(h, position);
	}

	void init()
	{
		settings::init();
		init_hooking();

		set_hooking();
		register_to_solutionhub();

		init_calltip();
	}

	void terminate()
	{
		shutdown_hooking();
	}

	void open_helpfile()
	{
		npp_open_file(npp_plugin::help_file());
	}

	void open_settings()
	{
		npp_open_file(npp_plugin::settings_file());
	}

	void reload_settings()
	{
		settings::load();
		set_hooking();
	}

	void goto_file_line()
	{
		using namespace npp;

		char B[4096];
		*B = 0;
		::SendMessage(npp_plugin::scintilla_view_current(), SCI_GETCURLINE, sizeof B-1, (LPARAM)B);

		decode_and_goto(B);
	}

	void goto_file_line_clip()
	{
		if (!OpenClipboard(0))
			return;

		HANDLE clipboard_data_handle = GetClipboardData(CF_TEXT);
		if (!clipboard_data_handle) {
			CloseClipboard();
			return;
		}

		char *clip_text = (char*)GlobalLock(clipboard_data_handle);
		if (!clip_text) {
			CloseClipboard();
			return;
		}

		const char *decode_buffer = 0;
		std::string tmpbuffer;
		unsigned clip_len = (unsigned)strlen(clip_text);
		if (clip_len < tempbuffer::BUFFERSIZE-1) {
			char *B = tempbuffer::buffer[1];
			decode_buffer = B;
			char *d = B;
			char *s = clip_text;
			while (*s) {
				*d = *s;
				++d; ++s;
			}
			*d = 0;
		} else {
			tmpbuffer = clip_text;
			decode_buffer = tmpbuffer.c_str();
		}

		GlobalUnlock(clipboard_data_handle);

		CloseClipboard();

		decode_and_goto(decode_buffer);
	}

	void goto_file()
	{
		using namespace npp;

		char *B = tempbuffer::buffer[1];
		*B = 0;
		::SendMessage(npp_plugin::scintilla_view_current(), SCI_GETCURLINE, tempbuffer::BUFFERSIZE-1, (LPARAM)B);

		if(*B) {
			B = stb_trimwhite(B); //! remove whites

			const char *first_open = first_open_char(B);
			if(!first_open)
				return;

			wchar_t *tbuffer = tempbuffer::widebuffer[3];
			npp_aux_document_get_current_extension(tbuffer, tempbuffer::BUFFERSIZE);
			if(*tbuffer == '.')
				++tbuffer;

			for(unsigned i=0, N= (unsigned)wcslen(tbuffer); i != N;++i)
				tbuffer[i] = towlower(tbuffer[i]);

			const std::vector<char> &include_start_tags = settings::language_include_map[tbuffer];

			if(!include_start_tags.empty())
			{
				const char *data = &include_start_tags[0];
				unsigned N = stream::unpack<unsigned>(data);
				bool handled = false;

				// const char *start_point = B; //! use this to advance ?

				while(N-- && !handled)
				{
					//! NTF : tag_length includes the null-terminator!
					unsigned tag_length = stream::unpack<unsigned>(data);
					unsigned str_len = tag_length-1;
					const char *check_tag = (const char*)data;

					const char *tag_start = strstr(B, check_tag);
					if(tag_start)
					{
						handled = true;

						unsigned b_len = (unsigned)strlen(B);
						if(str_len == b_len)
							B = 0;
						else
							B = B+str_len;
					}

					stream::advance(data, tag_length);
				}
			}
		}

		if(B)
			decode_currentline(B);
	}

	void switch_file()
	{
		using namespace npp;

		wchar_t T[tempbuffer::BUFFERSIZE];

		wchar_t *B = T;
		*B = 0;

		npp_aux_document_get_current_full(B, tempbuffer::BUFFERSIZE);

		if(*B) {
			str_replace_wide(B,  L'/', L'\\');

			const wchar_t *current_ext = file_util::fileextension(B, false); // do not keep dot
			if(!current_ext)
				return;

			const std::vector<char> &si_data = switch_map[current_ext];

			if(si_data.empty()) {
				DEBUG_PRINT("No configuration for extension : %S", (current_ext ? current_ext : L"none"));
				return;
			}

			unsigned check_file_len = (unsigned)((current_ext-B)-1); // -1 remove dot
			B[check_file_len] = 0;

			const char *data = &si_data[0];

			unsigned N = stream::unpack<unsigned>(data);
			bool handled = false;

			while(N-- && !handled)
			{
				unsigned string_length = stream::unpack<unsigned>(data);
				const wchar_t *check_ex = (const wchar_t*)data;

				const unsigned end = string_length+check_file_len;
				if (tempbuffer::BUFFERSIZE > end)
				{
					void *destination = &B[check_file_len];
					memcpy(destination, check_ex, string_length * sizeof(wchar_t));
					B[end] = 0;

					if (::PathFileExists(B)) {
						handled = true;
						npp_open_file(B);
					}
				}
				stream::advance(data, string_length*sizeof(wchar_t));

				DEBUG_PRINT("CHECK EXTENSION : %S, file(%S)", check_ex, B);
			}

			if(!handled && !switch_file_ofis_disabled)
			{
				B[check_file_len] = 0;
				const wchar_t *filename = file_util::filename(B);
				if(filename)
				{
					std::vector<char> search_data;

					stream::pack(search_data, HEADER_SWITCH);

					stream::pack_string_wide(search_data, filename);
					stream::pack_string_wide(search_data, current_ext);

					stream::pack_bytes(search_data, &si_data[0], (unsigned)si_data.size());

					search(filename, &search_data[0], (unsigned)search_data.size()); // send to SolutionHub
				}
			}
		}
	}

	void open_filerecord(const FileRecord &fr)
	{
		String temp(fr.path);
		temp += fr.filename;
		const wchar_t *s = temp.c_str();
		if(::PathFileExists(s))
			npp_open_file(s);
	}

	void on_searchresponse(void *indata)
	{
		using namespace npp;

		SearchResponse &sr = *((SearchResponse*)indata);
		const char *buffer = (const char *)sr.data;
		void *userdata = sr.userdata;

		FileRecords frs(buffer);

		if(!userdata) {
			if(frs.num_records != 1)
				return;

			FileRecord fr = frs.filerecord(0);

			open_filerecord(fr);
		} else {
			const char *data = (const char*)userdata;
			unsigned header = stream::unpack<unsigned>(data);

			if (header == HEADER_GOTO_FILELINE) {
				if(frs.num_records != 1)
					return;

				FileRecord fr = frs.filerecord(0);
				unsigned string_length = stream::unpack<unsigned>(data);
				const wchar_t *searchdata = (const wchar_t*)data;
				(void)searchdata;
				stream::advance(data, string_length*sizeof(wchar_t));
				int line = stream::unpack<int>(data);

				String temp(fr.path);
				temp += fr.filename;
				nppaux_document_open(temp.c_str(), line);

			} else if(header == HEADER_SEARCH) {
				//! searchfile
				unsigned string_length = stream::unpack<unsigned>(data);
				const wchar_t *searchdata = (const wchar_t*)data;
				stream::advance(data, string_length*sizeof(wchar_t));

				const wchar_t *searchfile = file_util::filename(searchdata);
				unsigned searchfile_length = (unsigned)wcslen(searchfile);

				unsigned num_records = frs.num_records;
				if(num_records == 1) {
					FileRecord fr = frs.filerecord(0);

					open_filerecord(fr);
				} else {
					bool handled = false;

					for(unsigned i=0; i != num_records && !handled ; ++i)
					{
						FileRecord fr = frs.filerecord(i);
						const wchar_t *filename = fr.filename;
						handled = (filename[searchfile_length] == L'.');

						if(handled)
							open_filerecord(fr);
					}
				}
			}
			else if(header == HEADER_SWITCH)
			{
				//! searchfile
				unsigned string_length = stream::unpack<unsigned>(data);
				const wchar_t *searchfile = (const wchar_t*)data;
				(void)searchfile;
				stream::advance(data, string_length*sizeof(wchar_t));

				//! extension
				string_length = stream::unpack<unsigned>(data);
				const wchar_t *searchfile_extension = (const wchar_t*)data;
				(void)searchfile_extension;
				stream::advance(data, string_length*sizeof(wchar_t));

				//!
				unsigned N = stream::unpack<unsigned>(data);
				bool handled = false;

				unsigned num_records = frs.num_records;
				if(!num_records)
					return;

				wchar_t T[MAX_PATH];

				while(N-- && !handled)
				{
					string_length = stream::unpack<unsigned>(data);
					const wchar_t *check_ex = (const wchar_t*)data;
					stream::advance(data, string_length*sizeof(wchar_t));

					for(unsigned i = 0; i < num_records && !handled; ++i)
					{
						FileRecord fr = frs.filerecord(i);
						if(!string_util::str_ends_with(fr.filename, check_ex, false))
							continue;

						unsigned l = (unsigned)wcslen(fr.path);
						unsigned offset = l;
						if(l >= MAX_PATH)
							continue;

						memcpy(T, fr.path, l*sizeof(wchar_t));
						l = (unsigned)wcslen(fr.filename);

						if((offset+l) >= MAX_PATH)
							continue;

						memcpy(&T[offset], fr.filename, l*sizeof(wchar_t));
						offset +=l;
						T[offset] = 0;

						if (::PathFileExists(T)) {
							handled = true;
							npp_open_file(T);
						}

					} // for num_records
				} // while !N
			}

		} // else
	}
}
