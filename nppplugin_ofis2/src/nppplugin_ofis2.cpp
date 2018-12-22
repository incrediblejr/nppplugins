#include "resource.h"

#include "nppplugin_ofis2.h"
#include "nppplugin_ofis2_messages.h"
#include "nppplugin_solutionhub_interface/nppplugin_solutionhub_com_interface.h"
#include "nppplugin_solutionhub_interface/filerecords.h"

#include "npp/plugin/npp_plugin_interface.h"

#include "win32/ui_aux.h"
#include "resizer/Resizer.h"
#include "json_aux/json_aux.h"

#include "string/string_utils.h"
#include "debug.h"

#include <algorithm>

// for SHAutoComplete
#include <shlwapi.h>
#pragma comment (lib, "shlwapi.lib")

typedef std::wstring String;

#ifndef VK_PAGE_UP
	#define VK_PAGE_UP VK_PRIOR
#endif

#ifndef VK_PAGE_DOWN
	#define VK_PAGE_DOWN VK_NEXT
#endif

#ifndef WM_MOUSEWHEEL
	#define WM_MOUSEWHEEL 0x020A
#endif

namespace {
	template<typename T, int STACKSIZE>
	struct scoped_buffer {
		explicit scoped_buffer() : buff(0) {
		}
		scoped_buffer(unsigned n) : buff(0) {
			allocate(n);
		}
		~scoped_buffer() {
			if (buff && (buff != stack))
				delete [] buff;
		}
		void allocate(unsigned n) {
			if (buff != 0)
				*((int*)0) = STACKSIZE;

			if (STACKSIZE>=n)
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
}

namespace settings {
	namespace ui {
		namespace filelist {
			DWORD hot, hot_back;
			DWORD selected, selected_back;
			DWORD even, even_back;
			DWORD odd, odd_back;

			bool draw_hot;
			void set_default()
			{
#ifdef _DEBUG
				draw_hot = true;
#else
				draw_hot = false;
#endif

				hot_back = RGB(255, 0, 0); hot = RGB(0,0,0);
				selected_back = RGB(255, 228, 181); selected = RGB(0,0,0);
				even_back = RGB(224,255,255); even = RGB(0,0,0);
				odd_back = RGB(255,255,255); odd = RGB(0,0,0);
			}
		}

		void parse(DWORD *res, const char *s)
		{
			unsigned r,g,b;
			int n = sscanf(s, "%u %u %u", &r, &g, &b);
			if(n == 3)
				*res = RGB(r, g, b);
		}

		namespace edit {
			String help_text;
			String no_connection_text;
			String *current_text;

			void set_default()
			{
				help_text = L"no help set";
				no_connection_text = L"No connection is set, please setup a connection in the SolutionHub UI";

				current_text = &help_text;
			}
		}
	}

	void parse_color(const Json::Value &c, const char *n, DWORD *col)
	{
		if(c[n].isString()) {
			const char *s = c[n].asCString();
			ui::parse(col, s);
		}
	}

	std::string allowed_characters;
	bool do_prefix_uppercase;
	char prefix_uppercase[16];
	unsigned prefix_len;

	void selection_set_default()
	{
		allowed_characters = "";
		do_prefix_uppercase = false;
		memset(prefix_uppercase, 0, sizeof(prefix_uppercase));
		prefix_len = 0;
	}

	void load() {
		selection_set_default();

		ui::filelist::set_default();
		ui::edit::set_default();

		Json::Value config;
		const String sf = npp_plugin::settings_file();
		if(json_aux::json_from_file(sf, config)) {
			allowed_characters = (	config["selection_allowed_characters"].isString() ?
									config["selection_allowed_characters"].asString() : "");

			do_prefix_uppercase = (config["prefix_uppercase"].isBool() ? config["prefix_uppercase"].asBool() : false);
			if(do_prefix_uppercase)
			{
				const char *pf = (config["uppercase_prefix"].isString() ? config["uppercase_prefix"].asCString() : 0);
				if(pf) {
					unsigned L = (unsigned)strlen(pf);
					if(sizeof(prefix_uppercase) > L) {
						strcpy(prefix_uppercase, pf);
						prefix_len = L;
					} else {
						*prefix_uppercase = 0;
						prefix_len = 0;
					}
				}
			}
			do_prefix_uppercase = (*prefix_uppercase != 0);

			if(config["edit_help"].isString()) {
				settings::ui::edit::help_text = string_util::to_wide(config["edit_help"].asCString());
			}

			if(config["edit_no_connection"].isString()) {
				settings::ui::edit::no_connection_text = string_util::to_wide(config["edit_no_connection"].asCString());
			}

			if(config["ui"].isObject())
			{
				const Json::Value &uic = config["ui"];
				parse_color(uic, "even", &ui::filelist::even);
				parse_color(uic, "even_back", &ui::filelist::even_back);
				parse_color(uic, "odd", &ui::filelist::odd);
				parse_color(uic, "odd_back", &ui::filelist::odd_back);
				parse_color(uic, "selected", &ui::filelist::selected);
				parse_color(uic, "selected_back", &ui::filelist::selected_back);
				parse_color(uic, "hot", &ui::filelist::hot);
				parse_color(uic, "hot_back", &ui::filelist::hot_back);

				ui::filelist::draw_hot = (uic["draw_hot"].isBool() ? uic["draw_hot"].asBool() : false);
			}

		} else {
			DEBUG_PRINT("[ofis2] Failed to parse settings.");
		}
	}
}

namespace npp {
	bool open_file(const String &full_filename)
	{
		const wchar_t *fn = full_filename.c_str();

		return (1 == ::SendMessage(npp_plugin::npp(), NPPM_DOOPEN, 0, (LPARAM)fn));
	}

	String selected_text(const char *valid_characters)
	{
		const int N = (int)::SendMessage(npp_plugin::scintilla_view_current(), SCI_GETSELTEXT, 0 , 0);

		if (0 >= N)
			return L"";

		scoped_buffer<char, 512> scoped(N+1);
		char *c = scoped;

		::SendMessage(npp_plugin::scintilla_view_current(), SCI_GETSELTEXT, 0, (LPARAM) c);

		bool pfuc = settings::do_prefix_uppercase;

		unsigned nupper = 0;

		const char *start = 0;
		char *s = c;
		while(*s) {
			int cc = *s;
			if(!string_util::isletter(cc) && !strchr(valid_characters, cc)) {
				if(start) {
					*s = 0;
					break;
				}
			} else {
				if(!start)
					start = s;

				nupper = ((pfuc && isupper(cc)) ? nupper+1 : nupper);
			}
			++s;
		}

		if(start) {
			if(!nupper)
				return string_util::to_wide(start);

			unsigned L = (unsigned)strlen(start);
			if(1 > L)
				return string_util::to_wide(start);

			const char *prefix = settings::prefix_uppercase;

			unsigned pfl = settings::prefix_len;

			scoped_buffer<char, 512> scoped_prefix(L+1+nupper);
			char *pfs = scoped_prefix;
			s = (char*) start;

			*s = (char)tolower(*s);

			while(*s) {
				if(isupper(*s)) {
					memcpy(pfs, prefix, pfl);
					pfs += (pfl);
				}

				*pfs = *s;
				++pfs;
				++s;
			}

			*pfs = 0;

			start = scoped_prefix;

			return string_util::to_wide(start);
		}

		return L"";
	}
}
namespace tempbuffers {
	const unsigned NUM_TEMPBUFFERS = 2;
	const unsigned BUFFER_SIZE = 4096;
	wchar_t buffer[NUM_TEMPBUFFERS][BUFFER_SIZE] = {};
}

namespace {
	std::vector<int> selected_items;
	int hot_index;
	int pivot_point;

	void filelist_clear_selections() {
		selected_items.clear();
		hot_index = 0;
		pivot_point = -1;
	}

	bool filelist_selected(int index) {
		return (selected_items.end() != std::find(selected_items.begin(), selected_items.end(), index));
	}

	void filelist_toggle_selection(int index) {
		unsigned count = (unsigned)selected_items.size();
		std::vector<int>::const_iterator i = std::find(selected_items.begin(), selected_items.end(), index);
		if(i==selected_items.end()) {
			selected_items.push_back(index);
			hot_index = index;
		} else if(count > 1) {
			selected_items.erase(i);
			hot_index = selected_items.back();
		}
	}

	void filelist_debug_selection() {
		#ifdef _DEBUG
			char B[64];
			unsigned C = (unsigned)selected_items.size();
			if(C) {
				std::string t("selected [");
				for(unsigned i=0; i<C;++i) {
					sprintf(B,"%d",selected_items[i]);
					t += B;
					if(i!=(C-1))
						t+= " ";
				}
				t += "], hot[";
				sprintf(B,"%d", hot_index);
				t+= B;
				t +="]";
				DEBUG_PRINT("%s", t.c_str());
			} else {
				std::string t("selected [ NONE ], hot[ ");
				sprintf(B,"%d", hot_index);
				t+= B;
				t +="]";
				DEBUG_PRINT("%s", t.c_str());
			}
		#endif
	}

	bool filelist_single_selection() { return (selected_items.size() == 1); }
	bool filelist_multi_selection() { return (selected_items.size() > 1); }

	void filelist_set_single_selectionindex(int index)	{
		filelist_clear_selections();
		filelist_toggle_selection(index);
	}

	int filelist_min() {
		if(selected_items.empty())
			return -1;

		return *(min_element( selected_items.begin(), selected_items.end() ));
	}

	int filelist_max() {
		if(selected_items.empty())
			return -1;

		return *(max_element( selected_items.begin(), selected_items.end() ));
	}

	void filelist_toggle_indices(int hot, int end_index)
	{
		if (hot == end_index)
			return;

		int direction, steps;
		if (end_index > hot) {
			direction = 1;
			steps = end_index-hot;
		} else {
			direction = -1;
			steps = hot-end_index;
		}

		while (steps) {
			hot += direction;
			filelist_toggle_selection(hot);
			--steps;
		}
	}
}

namespace {
	const char *bool_str(bool b) { return (b ? "true" : "false"); }
}

namespace {
	bool ctrl_pressed() { return 0x8000 == (::GetKeyState(VK_CONTROL) & 0x8000); }
	bool shift_pressed() { return 0x8000 == (::GetKeyState(VK_SHIFT) & 0x8000); }
}

namespace npp_plugin_ofis2 {
	namespace filelist {
		LRESULT custom_draw(HWND h, NMLVCUSTOMDRAW *cd);
	}

	namespace edit {
		WNDPROC org_message_proc = 0;

		LRESULT CALLBACK edit_message_proc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
	}
}

namespace {
	std::vector<char> record_data;
	FileRecords filerecords;

	HWND self_handle = 0;
	HWND edit_handle = 0;
	HWND list_handle = 0;

	ui_aux::listview::column filelist_columns[] = {
		{	ui_aux::listview::column::CT_PROPERTIONAL, 30, L"Name"	},
		{	ui_aux::listview::column::CT_PROPERTIONAL, 50, L"Path"	},
		{	ui_aux::listview::column::CT_PROPERTIONAL, 20, L"Date"	}
	};
	const int num_filelist_columns = sizeof(filelist_columns) / sizeof(filelist_columns[0]);

	CResizer resizer;

	HANDLE icons[2];

	INT_PTR main_message_proc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

	void on_show();
	void on_hide();

	void filelist_setup_columns();
	void filelist_fit_columns();
	void filelist_update_activeset();

	void icon_setup_titlebar(HWND h)
	{
		HINSTANCE p	= (HINSTANCE)npp_plugin::module();
		icons[0] = ::LoadImage(p, MAKEINTRESOURCE(IDI_ORANGE), IMAGE_ICON, ::GetSystemMetrics(SM_CXICON), ::GetSystemMetrics(SM_CYICON), 0);

		if(icons[0])
			::SendMessage(h, WM_SETICON, ICON_BIG, (LPARAM)(icons[0]));

		icons[1] = ::LoadImage(p, MAKEINTRESOURCE(IDI_ORANGE), IMAGE_ICON, ::GetSystemMetrics(SM_CXSMICON), ::GetSystemMetrics(SM_CYSMICON), 0);
		if(icons[1])
			::SendMessage(h, WM_SETICON, ICON_SMALL, (LPARAM)(icons[1]));
	}

	void solution_search(const wchar_t *s);
	void open_selected_files();
}

namespace npp_plugin_ofis2
{
	namespace internal
	{
		void register_to_solutionhub()
		{
			SolutionHubRegisterContext rc;
			rc.alias = "ofis";
			rc.mask = NPP_SH_RCMASK_INDEXING;

			CommunicationInfo comm;
			comm.internalMsg = NPPM_SOLUTIONHUB_HOOK_RECEIVER;
			comm.srcModuleName = npp_plugin::module_name();
			comm.info = &rc;

			::SendMessage(npp_plugin::npp(), NPPM_MSGTOPLUGIN, (WPARAM)SOLUTIONHUB_DLL_FILE_NAME, (LPARAM)&comm);
		}
	}

	void init()
	{
		settings::load();
	}

	void init_ui()
	{
		internal::register_to_solutionhub();

		HWND parent = npp_plugin::npp();
		self_handle = ::CreateDialogParam((HINSTANCE)npp_plugin::module(), MAKEINTRESOURCE(IDD_OFIS2_MAIN), parent, (DLGPROC)main_message_proc, 0);

		::SendMessage(parent, NPPM_MODELESSDIALOG, MODELESSDIALOGADD, (WPARAM)self_handle);
	}

	void terminate()
	{
		::DestroyIcon((HICON)icons[0]);
		::DestroyIcon((HICON)icons[1]);
	}

	void show_ui()
	{
		on_show();
	}

	void on_search_response(void *data)
	{
		SearchResponse &sr = *((SearchResponse*)data);
		const char *buffer = (const char *)sr.data;

		const unsigned datasize = sr.data_size;

		record_data.resize(datasize);
		memcpy(&record_data[0], buffer, datasize);

		filerecords = FileRecords(&record_data[0]);
#if 0
		if(sr.userdata) {
			const wchar_t *searchstring = (const wchar_t*)sr.userdata;
			DEBUG_PRINT("[ofis2] : searched for %S", searchstring);
		}

		unsigned max_records = min(10, filerecords.num_records);

		for(unsigned i=0; i < max_records;++i) {
			FileRecord fr = filerecords.filerecord(i);
			DEBUG_PRINT("[ofis2] fn(%S), p(%S), d(%S)", fr.filename, fr.path, fr.date);
		}

		DEBUG_PRINT("[ofis2] num records(%d)", filerecords.num_records);
#endif
		filelist_update_activeset();
	}

	void open_settings()
	{
		npp::open_file(npp_plugin::settings_file());
	}

	void reload_settings()
	{
		settings::load();
	}

	void open_helpfile()
	{
		const wchar_t *file = npp_plugin::help_file();

		::SendMessage(npp_plugin::npp(), NPPM_DOOPEN, 0, (LPARAM)file);
	}
}

namespace {
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


	void open_selected_files()
	{
		unsigned num_selected = (unsigned)selected_items.size();
		if (!num_selected || !filerecords.num_records)
		{
			 //! Ex. double-click on empty area
			on_hide();
			return;
		}

		String temp(L"");

		if (num_selected == 1) {
			int line = -1;
			wchar_t *s = tempbuffers::buffer[1];

			size_t slen = (size_t)::GetWindowText(edit_handle, s, tempbuffers::BUFFER_SIZE);
			size_t l = slen;
			size_t first_of = 0;

			while (l--) {
				wchar_t c = s[l];
				if (c == L':' || c == L'(')
					first_of = l;
			}

			#define IS_NUMBER(x) ((x) >= '0' && (x) <= '9')
			if (first_of) {
				size_t number_start = 0, number_end = 0;

				while (s[first_of++]) {
					if (IS_NUMBER(s[first_of])) {
						if (!number_start)
							number_start = first_of;
					} else if (number_start) {
						number_end = first_of;
						break;
					}
				}

				if (number_end > number_start) {
					size_t nlen = number_end-number_start;

					wchar_t b[64] = {0};
					if (((sizeof(b)/sizeof(*b))-1) >= nlen) {
						memcpy(b, &s[number_start], nlen*sizeof *s);
						b[nlen] = 0;
						line = _wtoi(b);
					}
				}
			}
			#undef IS_NUMBER

			unsigned index = (unsigned)selected_items[0];

			if((filerecords.num_records) > index) {
				FileRecord fr = filerecords.filerecord(index);
				temp = fr.path;
				temp += fr.filename;

				nppaux_document_open(temp.c_str(), line);
			} else {
				DEBUG_PRINT("[ofis2] ERROR : Selected index is a invalid filerecord");
			}
		} else {
			for(unsigned i=0;i<num_selected;++i) {
				unsigned index = (unsigned)selected_items[i];

				if((filerecords.num_records) > index) {
					FileRecord fr = filerecords.filerecord(index);
					temp = fr.path;
					temp += fr.filename;

					npp::open_file(temp);
				} else {
					DEBUG_PRINT("[ofis2] ERROR : Selected index is a invalid filerecord");
				}
			}
		}
	}

	void solution_search(const wchar_t *s)
	{
		size_t slen = wcslen(s);
		scoped_buffer<wchar_t, 512> buffer;
		const wchar_t *searchstring;

		size_t l = slen;
		size_t first_of = 0;
		while (l--) {
			wchar_t c = s[l];
			if (c == L':' || c == L'(')
				first_of = l;
		}

		if (first_of) {
			buffer.allocate((unsigned)first_of+1);
			memcpy(buffer.ptr(), s, first_of*sizeof *s);
			buffer.ptr()[first_of] = 0;
			searchstring = buffer.ptr();
		} else
			searchstring = s;

		SearchRequest sr; memset(&sr, 0, sizeof(sr));
		sr.result_notification = NPPM_OFIS_ON_SEARCH_RESPONSE;
		sr.searchstring = searchstring;

		CommunicationInfo comm;
		comm.internalMsg = NPPM_SOLUTIONHUB_SEARCH_SOLUTION;
		comm.srcModuleName = npp_plugin::module_name();
		comm.info = &sr;

		LRESULT msg_res = ::SendMessage(npp_plugin::npp(), NPPM_MSGTOPLUGIN, (WPARAM)SOLUTIONHUB_DLL_FILE_NAME, (LPARAM)&comm);

		if(msg_res && sr.result != SolutionHubResults::SH_NO_ERROR) {
			if(sr.result == SolutionHubResults::SH_ERROR_NO_CONNECTION) {
				settings::ui::edit::current_text = &settings::ui::edit::no_connection_text;
			}
		} else if(msg_res) {
			settings::ui::edit::current_text = &settings::ui::edit::help_text;
		}
	}

	void filelist_clear_ui()
	{
		::SendMessage(list_handle, LVM_SETITEMCOUNT, 0, 0);
		::UpdateWindow(list_handle);
	}

	void filelist_update_activeset()
	{
		filelist_clear_selections();

		::SendMessage(list_handle, WM_SETREDRAW, 0, 0);	// disable rendering

		const unsigned num = filerecords.num_records;
		::SendMessage(list_handle, LVM_SETITEMCOUNT, num, 0);

		if(num > 0)
		{
			filelist_toggle_selection(0);

			::SendMessage(list_handle, LVM_ENSUREVISIBLE, 0, 0);

			ListView_RedrawItems(list_handle, 0, 0);
		}
		::SendMessage(list_handle, WM_SETREDRAW, 1, 0); // enable rendering
		::UpdateWindow(list_handle);
	}

	void filelist_fit_columns()
	{
		ui_aux::listview::column_fit(list_handle, filelist_columns, num_filelist_columns);
	}

	void filelist_setup_columns()
	{
		ui_aux::listview::column_setup(list_handle, filelist_columns, num_filelist_columns);
		filelist_fit_columns();
	}

	void on_show()
	{
		filerecords.num_records = 0;

		String search_str = npp::selected_text(settings::allowed_characters.c_str());

		if(!search_str.empty()) {
			::SendMessage(edit_handle, WM_SETTEXT, 0, (LPARAM)search_str.c_str());
		} else {
			//! Fake a EN_CHANGE to search on open
			::SendMessage(self_handle, WM_COMMAND, MAKEWPARAM((UINT)(IDC_OFIS2_EDIT),(UINT)(EN_CHANGE)), (LPARAM)(HWND)(edit_handle));
		}

		::SendMessage(edit_handle, EM_SETSEL, 0,  -1);		// select all to place caret at end, do this regardless

		ui_aux::center_window(self_handle);
		::ShowWindow(self_handle, SW_SHOW);
	}

	void on_hide()
	{
#if !defined(_DEBUG)
		filelist_clear_ui();
		::ShowWindow(self_handle, SW_HIDE);
#endif
	}

	//!-------------------------------------------------------------------------------------------
	//	MSG-PROC BELOW
	//
	void on_size_main(HWND hwnd, UINT state, int cx, int cy)
	{
		resizer.Move();
		filelist_fit_columns();

		return FORWARD_WM_SIZE(hwnd, state, cx, cy, ::DefWindowProc);
	}

	void on_activate_main(HWND hwnd, UINT state, HWND hwndActDeact, BOOL fMinimized)
	{
		if (state == WA_INACTIVE) {
			on_hide();
		} else {
			return FORWARD_WM_ACTIVATE(hwnd, state, hwndActDeact, fMinimized, ::DefWindowProc);
		}
	}

	BOOL on_initdialog_main(HWND hwnd, HWND hwndFocus, LPARAM lParam)
	{
		edit_handle = ::GetDlgItem(hwnd, IDC_OFIS2_EDIT);
		SHAutoComplete(edit_handle, SHACF_AUTOSUGGEST_FORCE_OFF); // to get 'CTRL+backspace' logic
		npp_plugin_ofis2::edit::org_message_proc = (WNDPROC)::SetWindowLongPtr(edit_handle, GWLP_WNDPROC, (LONG_PTR)npp_plugin_ofis2::edit::edit_message_proc);

		list_handle = ::GetDlgItem(hwnd, IDC_OFIS2_LIST);

		DWORD ex_style = ListView_GetExtendedListViewStyle(list_handle) | LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_GRIDLINES;
		ListView_SetExtendedListViewStyle(list_handle, ex_style);

		static CResizer::CBorderInfo s_bi[] = {
			{IDC_OFIS2_EDIT,    {CResizer::eFixed, IDC_MAIN,		CResizer::eLeft}, //Left side
								{CResizer::eFixed, IDC_MAIN,		CResizer::eTop},  //Top side
								{CResizer::eFixed, IDC_MAIN,		CResizer::eRight}, //Right side
								{CResizer::eFixed, IDC_MAIN,		CResizer::eTop}}, //Bottom side

			{IDC_OFIS2_LIST,	{CResizer::eFixed, IDC_OFIS2_EDIT,	CResizer::eLeft},
								{CResizer::eFixed, IDC_OFIS2_EDIT,	CResizer::eBottom},
								{CResizer::eFixed, IDC_OFIS2_EDIT,	CResizer::eRight},
								{CResizer::eFixed, IDC_MAIN,		CResizer::eBottom}},
		};

		const int rsize = sizeof(s_bi)/sizeof(s_bi[0]);
		resizer.Init(hwnd, 0, s_bi, rsize);

		filelist_setup_columns();

		icon_setup_titlebar(hwnd);

		return FORWARD_WM_INITDIALOG(hwnd, hwndFocus, lParam, ::DefWindowProc);
	}

	void on_command_main(HWND hwnd, int id, HWND hwndCtl, UINT code)
	{
		if (id == IDCANCEL)	{
			on_hide();
		} else if(id == IDOK) {
			open_selected_files();
		} else if(id == IDC_OFIS2_EDIT) {
			if(code == EN_CHANGE) {
				wchar_t *buffer = tempbuffers::buffer[0];

				const int n = ::GetWindowText(edit_handle, buffer, tempbuffers::BUFFER_SIZE);
				solution_search(buffer);
			} else if(code == EN_UPDATE) {
				if (::GetWindowTextLength(edit_handle) == 0)
					::InvalidateRect(edit_handle, 0, TRUE); // repaint
			}
		} else {
			return FORWARD_WM_COMMAND(hwnd, id, hwndCtl, code, ::DefWindowProc);
		}
	}

	LRESULT on_notify_main(HWND hwnd, int idFrom, NMHDR *nmhdr)
	{
		unsigned code = nmhdr->code;
		if (idFrom == IDC_OFIS2_LIST) {

			if(code == WM_NOTIFY) {
				DEBUG_PRINT("WM_NOTIFY");
			} else if(code == NM_SETFOCUS) {
				::SetFocus(edit_handle);
			} else if(code == NM_CLICK || code == NM_RCLICK) {
				NMITEMACTIVATE *header = (NMITEMACTIVATE *)nmhdr;

				if(!ctrl_pressed()) {
					if (shift_pressed()) {
						filelist_toggle_indices(hot_index, header->iItem);
					} else
						filelist_set_single_selectionindex(header->iItem);

				} else {
					filelist_toggle_selection(header->iItem);
				}
				::InvalidateRect(list_handle, 0, TRUE); // repaint

				return FALSE;
			} else if(code == NM_DBLCLK) {
				NMITEMACTIVATE *header = (NMITEMACTIVATE *)nmhdr;

				filelist_set_single_selectionindex(header->iItem);

				open_selected_files();
			} else if(code == LVN_GETDISPINFO) {
				NMLVDISPINFO *pdi = (NMLVDISPINFO*)nmhdr;
				LVITEM &item = (*pdi).item;

				if(item.mask & LVIF_TEXT)
				{
					const int index = item.iItem;
					const int sub_index = item.iSubItem;

					if(filerecords.num_records && (filerecords.num_records-1) >= (unsigned)index) {
						FileRecord fr = filerecords.filerecord(index);

						if(sub_index==0)
							item.pszText = (wchar_t*)fr.filename;
						else if(sub_index==1)
							item.pszText = (wchar_t*)fr.path;
						else if(sub_index==2)
							item.pszText = (wchar_t*)fr.date;

					} else {
						item.pszText = L"ME = FAIL";
					}
				}

			} else if (code == NM_CUSTOMDRAW) {
				NMLVCUSTOMDRAW *lv_cd = (NMLVCUSTOMDRAW*)nmhdr;
				::SetWindowLongPtrW(self_handle,
#if _WIN64
									DWLP_MSGRESULT,
#else
									DWL_MSGRESULT,
#endif
									(LONG_PTR) npp_plugin_ofis2::filelist::custom_draw(list_handle, lv_cd)
									);
				return TRUE;
			}
		}

		return FORWARD_WM_NOTIFY(hwnd, idFrom, nmhdr, ::DefWindowProc);
	}

	void on_key_main(HWND hwnd, UINT vk, BOOL fDown, int cRepeat, UINT flags)
	{
		if (!fDown)
			return FORWARD_WM_KEYDOWN(hwnd, vk, cRepeat, flags, ::DefWindowProc);

		unsigned num_records = filerecords.num_records;

		if(!num_records)
			return FORWARD_WM_KEYDOWN(hwnd, vk, cRepeat, flags, ::DefWindowProc);

		//! Probably could figure out if ctrl/shift was pressed by flags but ...
		bool ctrl = ctrl_pressed(), shift = shift_pressed();

		if(vk == VK_UP || vk == VK_DOWN) {
			int top_index = (int)::SendMessage(list_handle, LVM_GETTOPINDEX, 0, 0);
			int count_pp = (int)::SendMessage(list_handle, LVM_GETCOUNTPERPAGE, 0, 0);

			int end_record_index = ((int)num_records)-1;
			int bottom_vis = top_index+count_pp;

			bool hot_index_visible = (hot_index >= top_index && bottom_vis >= hot_index);

			bool up = (vk == VK_UP);//, down = (vk == VK_DOWN);
			bool toggle_select = (shift || ctrl);

			DEBUG_PRINT("hot_index_visible(%s), hot_index(%d), top(%d), bottom(%d)",
						bool_str(hot_index_visible), hot_index, (int)top_index, bottom_vis);

			bool change_selection =	hot_index_visible;
			if(change_selection) {
				bool do_wrap = !toggle_select;

				int current_index = hot_index;
				int new_index;
				if(up) {
					new_index = current_index-1;
					if(new_index == -1)
						new_index = (do_wrap ? ((int)num_records)-1 : 0);
				} else {
					new_index = current_index+1;
					if(new_index > ((int)num_records)-1)
						new_index = (do_wrap ? 0 : ((int)num_records)-1);
				}

				bool selection_changed = current_index != new_index;

				if(selection_changed) {
					if(!toggle_select) {
						filelist_set_single_selectionindex(new_index);
					} else {
						if(pivot_point == -1)
							pivot_point = current_index;

						if(current_index == pivot_point) {
							filelist_toggle_selection(new_index);
						} else if(!filelist_selected(new_index)) {
							filelist_toggle_selection(new_index);
						} else if(filelist_selected(new_index)) {
							filelist_toggle_selection(current_index);
							hot_index = new_index;
						}
					}

					::SendMessage(list_handle, LVM_ENSUREVISIBLE, new_index, 0);
					::InvalidateRect(list_handle, 0, TRUE); // repaint
				}
			} else {
				if((count_pp > ((int)num_records)))
					return FORWARD_WM_KEYDOWN(hwnd, vk, cRepeat, flags, ::DefWindowProc);

				int visible_index;

				if(up) {
					visible_index = top_index-1;
					if(0 > visible_index)
						visible_index = end_record_index;
				} else {
					visible_index = bottom_vis+1;
					if(visible_index > end_record_index)
						visible_index = 0;
				}

				::SendMessage(list_handle, LVM_ENSUREVISIBLE, visible_index, 0);
			}
		} else if(vk == VK_PAGE_UP || vk == VK_PAGE_DOWN || vk == VK_HOME || vk == VK_END) {
			DEBUG_PRINT("VK_PAGE_UP/VK_PAGE_DOWN : SHIFT(%s), CTRL(%s)", bool_str(shift), bool_str(ctrl));

			bool up = (vk == VK_PAGE_UP), down = (vk == VK_PAGE_DOWN), home = (vk == VK_HOME), end(vk == VK_END);
			if(ctrl || shift || home || end) {
				unsigned visible_index = ((up||home) ? 0 : (num_records > 0 ? num_records-1 : 0));

				filelist_clear_selections();
				filelist_toggle_selection(visible_index);
				::SendMessage(list_handle, LVM_ENSUREVISIBLE, visible_index, 0);
			} else {
				WPARAM direction = (up ? SB_PAGEUP : SB_PAGEDOWN);

				::SendMessage(list_handle, WM_VSCROLL, direction, 0);
				int count_pp = (int)::SendMessage(list_handle, LVM_GETCOUNTPERPAGE, 0, 0);
				int top_index = (int)::SendMessage(list_handle, LVM_GETTOPINDEX, 0, 0);

				if(filelist_single_selection() && num_records) {
					//unsigned current_selection = hot_index;
					int new_index;
					if(down) {
						new_index = hot_index+count_pp;
						new_index = (new_index > (int)(num_records-1) ? num_records-1 : new_index);
					} else {
						new_index = hot_index-count_pp;
						new_index = (0 > new_index ? 0 : min(new_index, top_index));
					}

					filelist_set_single_selectionindex(new_index);
					::InvalidateRect(list_handle, 0, TRUE); // repaint
				}
				DEBUG_PRINT("Count per page : %d, top index(%d)", (int)count_pp, (int)top_index);
			}
		}

		return FORWARD_WM_KEYDOWN(hwnd, vk, cRepeat, flags, ::DefWindowProc);
	}

	INT_PTR main_message_proc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
	{
		switch(message)
		{
			HANDLE_MSG(hwnd,	WM_NOTIFY,			on_notify_main);
			HANDLE_MSG(hwnd,	WM_INITDIALOG,		on_initdialog_main);
			HANDLE_MSG(hwnd,	WM_COMMAND,			on_command_main);
			HANDLE_MSG(hwnd,	WM_SIZE,			on_size_main);
			HANDLE_MSG(hwnd,	WM_ACTIVATE,		on_activate_main);
			HANDLE_MSG(hwnd,	WM_KEYDOWN,			on_key_main);
			HANDLE_MSG(hwnd,	WM_KEYUP,			on_key_main);
			case WM_MOUSEWHEEL :
			{
				return ::SendMessage(list_handle, WM_MOUSEWHEEL, wParam, lParam);
			}
			default:
				return FALSE;
		}
	}
}

namespace npp_plugin_ofis2 {
	namespace filelist {
		LRESULT custom_draw(HWND , NMLVCUSTOMDRAW *cd) {
			switch(cd->nmcd.dwDrawStage)
			{
			//Before the paint cycle begins
			case CDDS_PREPAINT :
				{
					return CDRF_NOTIFYITEMDRAW;
				}
			//Before an item is drawn
			case CDDS_ITEMPREPAINT:
				{
					int current_index = (int)cd->nmcd.dwItemSpec;

					bool is_selected = filelist_selected(current_index);
					bool is_hot = settings::ui::filelist::draw_hot && (hot_index == current_index);
					cd->nmcd.uItemState &= ~CDIS_SELECTED;
					cd->nmcd.uItemState &= ~CDIS_FOCUS;

					if(is_hot) {
						cd->clrText   = settings::ui::filelist::hot;
						cd->clrTextBk = settings::ui::filelist::hot_back;
					} else if (is_selected) {
						cd->clrText   = settings::ui::filelist::selected;
						cd->clrTextBk = settings::ui::filelist::selected_back;
					} else if ((current_index % 2) == 0)	{
						cd->clrText   = settings::ui::filelist::even;
						cd->clrTextBk = settings::ui::filelist::even_back;
					} else {
						cd->clrText   = settings::ui::filelist::odd;
						cd->clrTextBk = settings::ui::filelist::odd_back;
					}

					return CDRF_NEWFONT;
				}

			//Before a subitem is drawn
			case CDDS_SUBITEM | CDDS_ITEMPREPAINT:
				{
					return CDRF_NEWFONT;
				}
			}

			return  CDRF_DODEFAULT;
		}
	} // filelist

	namespace edit {
#if 0
		static inline int iswhite(int c) { return (isspace(c) || c == '\n' || c == '\r'); }
		static inline int isdivider(int c) { return iswhite(c) || c == '.' || c == ':'; }

		// returns the new length (i.e. pointing to last white)
		static unsigned find_non_white(const wchar_t *begin, const wchar_t *s)
		{
			while (begin != s) {
				int c = *s;
				if (!iswhite(c)) {
					++s;
					break;
				}
				--s;

			}
			return (unsigned)(s-begin);
		}

		static unsigned find_divider(const wchar_t *begin, const wchar_t *s)
		{
			while (begin != s) {
				int c = *s;
				if (isdivider(c)) {
					++s;
					break;
				}
				--s;

			}
			return (unsigned)(s-begin);
		}
#endif
		LRESULT CALLBACK edit_message_proc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
		{
			switch(message)
			{
			case WM_KEYDOWN :
				{
					WPARAM vk = wParam;
#if 0
					if (vk == VK_BACK) {
						int ctrl_down = GetKeyState(VK_CONTROL) & 0x8000;
						if (ctrl_down) {
							DWORD startc, endc;
							BOOL r = SendMessage(hwnd, EM_GETSEL, (WPARAM)&startc,(LPARAM)&endc);
							wchar_t *buffer = tempbuffers::buffer[1];
							//wchar_t *scratch = tempbuffers::buffer[1];
							unsigned slen = (unsigned)::GetWindowText(edit_handle, buffer, tempbuffers::BUFFER_SIZE);

							if (startc == endc) {
								if (startc) {
									int char_before_caret = buffer[startc-1];
									const wchar_t *end = buffer + startc - 1;
									unsigned pos = 0;
									if (iswhite(char_before_caret)) {
										pos = find_non_white(buffer, end);
									} else {
										pos = find_divider(buffer, end);
									}

									buffer[pos] = 0;
								}
							} else {
								unsigned num_chars_selected = endc-startc;
								if (num_chars_selected == slen) {
									// remove all
								} else {

									// selected text from startc -> endc
									wchar_t *movedest = buffer + startc;
									wchar_t *movestart = buffer + endc;
									unsigned bytes_to_move = (slen-num_chars_selected)*sizeof(wchar_t);
									memmove(movedest, movestart, bytes_to_move);
									bytes_to_move = 0;
								}
							}

							ctrl_down = 0;
							DEBUG_PRINT("setting text [%S]", buffer);
							::SendMessage(hwnd, WM_SETTEXT, 0, (LPARAM)buffer);
							return 0; // have handled
						}

					}
#endif
					if (vk == VK_DOWN || vk == VK_UP || vk == VK_PAGE_UP || vk == VK_PAGE_DOWN)
					{
						::SendMessage(::GetParent(hwnd), WM_KEYDOWN, wParam, lParam);
						return 0; // An application should return zero if it processes this message.
					}
				}
				break;
			case WM_PAINT :
				{
					int N = ::GetWindowTextLength(hwnd);
					if(0 >= N)
					{
						LRESULT margins	= ::SendMessage(hwnd, (UINT) EM_GETMARGINS, 0L, 0L);
						HFONT current_font = (HFONT) ::SendMessage(hwnd, (UINT) WM_GETFONT, 0L, 0L);
						PAINTSTRUCT ps;
						HDC hdc = BeginPaint(hwnd, &ps);
						HFONT old_font = (HFONT)::SelectObject(hdc, current_font);
						RECT rc;
						::GetClientRect( hwnd, &rc);

						::FillRect(hdc, &rc, (HBRUSH)(UINT_PTR)GetSysColor(COLOR_WINDOW));
						rc.left += (LOWORD(margins) + 1);
						rc.right -= (HIWORD(margins) + 1);
						COLORREF color = ::GetTextColor(hdc);
						SetTextColor(hdc, ::GetSysColor(COLOR_GRAYTEXT));
						const wchar_t *text = settings::ui::edit::current_text->c_str();

						::DrawText(hdc, text, -1, &rc, DT_CENTER|DT_SINGLELINE|DT_EDITCONTROL);
						::SetTextColor(hdc, color);
						::SelectObject(hdc, old_font);

						EndPaint(hwnd, &ps);
					}

				}
				break;
			}

			if (org_message_proc)
				return ::CallWindowProc(org_message_proc, hwnd, message, wParam, lParam);
			else
				return ::DefWindowProc(hwnd, message, wParam, lParam);
		}
	}
}
