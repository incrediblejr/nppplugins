#include "nppplugin_solutionhub_ui.h"
#include "nppplugin_solutionhub_interface/nppplugin_solutionhub_com_interface.h"

#include "npp/plugin/npp_plugin_interface.h"

#include "win32/ui_aux.h"
#include "resizer/Resizer.h"

#include "json_aux/json_aux.h"
#include "string/string_utils.h"
#include "debug.h"
#include "treelist/TreeListWnd.h"

#include "resource.h"
#include <Shlwapi.h> // path
#include <assert.h>

namespace {
	template <typename T>
	struct key_value_pair_t { T key; T value; };
	typedef key_value_pair_t<wchar_t*> kv_pair_wstr;
}

namespace treelist {
	void delete_selected(HWND h) {
		HTREEITEM si = TreeList_GetSelection(h);
		if (si)
			TreeList_DeleteItem(h, si);
	}

	bool get_string_field(HWND h, HTREEITEM i, int column, std::wstring &res) {
		TV_ITEM item; memset(&item, 0, sizeof(item));
		item.mask = TVIF_TEXT | TVIF_TEXTPTR | TVIF_SUBITEM;
		item.hItem = i;
		item.cChildren = column;

		if (TreeList_GetItem(h, &item)) {
			wchar_t *v = (item.cchTextMax ? item.pszText : 0);
			if (v)
				res.assign(v);

			return (v != 0);
		}

		return false;
	}

	static kv_pair_wstr get_kv_pair_wstr(HWND h, HTREEITEM i) {
		kv_pair_wstr result = { 0, 0 };

		TV_ITEM item; memset(&item, 0, sizeof(item));
		item.mask = TVIF_TEXT | TVIF_TEXTPTR | TVIF_SUBITEM;
		item.hItem = i;
		item.cChildren = 0;

		if (TreeList_GetItem(h, &item)) {
			wchar_t *key = (item.cchTextMax ? item.pszText : 0);
			if (key) {
				result.key = key;
				item.cChildren = 1;
				if (TreeList_GetItem(h, &item)) {
					result.value = (item.cchTextMax ? item.pszText : 0);
				}
			}
		}

		return result;
	}

	unsigned get_kv_pairs(HWND h, Json::Value &dest, bool include_empty) {
		dest = Json::Value(Json::objectValue);

		HTREEITEM i = TreeList_GetRoot(h);

		while (i) {
			kv_pair_wstr p = get_kv_pair_wstr(h, i);
			if (p.key && (p.value ? true : include_empty)) {
				dest[string_util::from_wide(p.key)] = (p.value ? string_util::from_wide(p.value) : "");
			}
			i = TreeList_GetNextSibling(h, i);
		}

		return dest.size();
	}

	void add_string_field(HWND h, const wchar_t *field) {
		TVINSERTSTRUCT insert;
		insert.hParent = TVI_ROOT;
		insert.hInsertAfter = TVI_LAST;
		TV_ITEM &i = insert.item;
		memset(&i, 0, sizeof(i));
		i.mask = TVIF_TEXT | TVIF_SUBITEM;
		i.cChildren = 0; // column (for clarity)

		i.pszText = (wchar_t*)field;

		TreeList_InsertItem(h, &insert);
	}

	void set_kv_pairs(HWND h, const Json::Value &source) {
		if (!source.isObject())
			return;

		TreeList_DeleteAllItems(h);

		TVINSERTSTRUCT insert;
		insert.hParent = TVI_ROOT;
		insert.hInsertAfter = TVI_LAST;
		TV_ITEM &i = insert.item;
		memset(&i, 0, sizeof(i));
		i.mask = TVIF_TEXT | TVIF_SUBITEM;
		i.cChildren = 0; // column (for clarity)

		TV_ITEM sec_col; memset(&sec_col, 0, sizeof(sec_col));
		sec_col.mask = TVIF_SUBITEM | TVIF_TEXT;
		sec_col.cChildren = 1; // column

		HTREEITEM item = 0;

		std::vector<std::string> members = source.getMemberNames();
		for (unsigned index = 0; index < members.size(); ++index) {
			const std::string &key = members[index];

			std::wstring key_w = string_util::utf8_to_wstr(key.c_str());
			i.pszText = (wchar_t*)key_w.c_str();

			item = TreeList_InsertItem(h, &insert);

			const std::string &val = source[key].asString();
			std::wstring val_w = string_util::utf8_to_wstr(val.c_str());
			sec_col.pszText = (wchar_t*)val_w.c_str();
			sec_col.hItem = item;

			TreeList_SetItem(h, &sec_col);
		}

		if (item)
			TreeList_EnsureVisible(h, item);

	}

	void add_kv_pair(HWND h, const wchar_t *key, const wchar_t *value)
	{
		TVINSERTSTRUCT insert;
		insert.hParent = TVI_ROOT;
		insert.hInsertAfter = TVI_LAST;
		TV_ITEM &i = insert.item;
		memset(&i, 0, sizeof(i));
		i.mask = TVIF_TEXT | TVIF_SUBITEM;
		i.cChildren = 0; // column (for clarity)

		TV_ITEM sec_col; memset(&sec_col, 0, sizeof(sec_col));
		sec_col.mask = TVIF_SUBITEM | TVIF_TEXT;
		sec_col.cChildren = 1; // column

		i.pszText = (wchar_t*)key;

		HTREEITEM item = TreeList_InsertItem(h, &insert);
		sec_col.pszText = (wchar_t*)value;
		sec_col.hItem = item;

		TreeList_SetItem(h, &sec_col);
	}

}

namespace tempbuffers {
	const unsigned NUM_TEMPBUFFERS = 2;
	const unsigned BUFFER_SIZE = 4096*16;
	char buffer[NUM_TEMPBUFFERS][BUFFER_SIZE] = {};
}

namespace {
	HWND self = 0;

	HWND combobox_templates = 0;
	HWND treelist_templates = 0;
	HWND treelist_folders = 0;
	HWND treelist_solutions = 0;
	HIMAGELIST image_list = 0;
}

namespace
{
	void show_fail_dialog(const char *body)
	{
		const char *title =	"SolutionHubUI";
		::MessageBoxA(npp_plugin::npp(), body, title, MB_ICONERROR | MB_OK);
	}
}

namespace {

	void treelist_templates_add_emptyfield(HWND h);

	const int TEMPLATE_TREELIST_ID = 42;
	const int FOLDERS_TREELIST_ID = 43;
	const int SOLUTIONS_TREELIST_ID = 44;

	void on_show();

	// definitions, move elsewhere
	struct ProjectTemplate {
		std::string name;
		std::vector<std::string> values;
	};

	std::map<std::string, ProjectTemplate> project_templates;

	std::string named_connections;

	Json::Value connections;
	Json::Value solutions;
	std::map<std::wstring, Json::Value> solution_map;

	void parse_template_defs() {
		project_templates.clear();
		Json::Value template_defs(Json::objectValue);

		std::wstring full_file(npp_plugin::settings_path());
		full_file += L"nppplugin_solutionhub_template_defs.settings";
		if(!json_aux::json_from_file(full_file, template_defs)) {
			DEBUG_PRINT("Failed to parse template definitions!");
			return;
		}

		std::vector<std::string> members = template_defs.getMemberNames();
		for(unsigned i=0, s=(unsigned)members.size(); i!=s; ++i) {
			const std::string template_name = members[i];

			ProjectTemplate &ct = project_templates[template_name];
			ct.name = template_name;

			Json::Value &def = template_defs[template_name];
			if(def.isArray()) {
				for(unsigned j=0, ds=def.size(); j!=ds; ++j) {
					ct.values.push_back(def[j].asString());
				}
			}
		}
	}

	void parse_solutions_from_settings(const Json::Value &json_sol, const Json::Value &json_connections) {
		connections.clear();
		solutions.clear();
		solution_map.clear();

		std::vector<std::string> members = json_sol.getMemberNames();
		for(unsigned i=0, s=(unsigned)members.size(); i!=s; ++i) {
			const std::string solution_name = members[i];
			const std::wstring wname = string_util::to_wide(solution_name.c_str());

			solution_map[wname] = json_sol[solution_name];
		}

		solutions = json_sol;
		connections = json_connections;
	}

	void parse_settings()
	{
		char temp_fail_buffer[256];
		std::vector<char> workbuffer;

		const wchar_t *self_name = npp_plugin::module_name();
		CommunicationInfo comm;
		comm.srcModuleName = self_name;

		GetBufferSizeRequest gbsr; memset(&gbsr, 0, sizeof(gbsr));
		gbsr.result = -2;

		comm.internalMsg = NPPM_SOLUTIONHUB_CONFIG_GET_SOLUTIONS_BUFFERSIZE;
		comm.info = &gbsr;
		LRESULT msg_res = ::SendMessage(npp_plugin::npp(), NPPM_MSGTOPLUGIN, (WPARAM)SOLUTIONHUB_DLL_FILE_NAME, (LPARAM)&comm);
		if(!msg_res)
			gbsr.result = -1;

		if(gbsr.result != SolutionHubResults::SH_NO_ERROR) {
			sprintf(temp_fail_buffer, "Failed to get buffersize for solutions. Error(%d)", gbsr.result);
			show_fail_dialog(temp_fail_buffer);
			return;
		}

		unsigned resize_size = gbsr.buffer_size*2;
		workbuffer.resize(resize_size);

		GetSolutionsRequest gsr; memset(&gsr, 0, sizeof(gsr));
		gsr.result = -2;
		gsr.solution_json = &workbuffer[0];
		gsr.buffer_size = resize_size;

		comm.internalMsg = NPPM_SOLUTIONHUB_CONFIG_GET_SOLUTIONS;
		comm.info = &gsr;

		msg_res = ::SendMessage(npp_plugin::npp(), NPPM_MSGTOPLUGIN, (WPARAM)SOLUTIONHUB_DLL_FILE_NAME, (LPARAM)&comm);
		if(!msg_res)
			gsr.result = -1;

		if(gsr.result != SolutionHubResults::SH_NO_ERROR) {
			sprintf(temp_fail_buffer, "Failed to get solutions. Error(%d)", gsr.result);
			show_fail_dialog(temp_fail_buffer);
			return;
		}

		Json::Value got_solutions;
		if(!json_aux::json_from_string(gsr.solution_json, got_solutions)) {
			sprintf(temp_fail_buffer, "Failed to parse solutions. Error(%d)", gsr.result);
			show_fail_dialog(temp_fail_buffer);
			return;
		}

		memset(&gbsr, 0, sizeof(gbsr));
		gbsr.result = -2;

		comm.internalMsg = NPPM_SOLUTIONHUB_CONFIG_GET_CONNECTIONS_BUFFERSIZE;
		comm.info = &gbsr;
		msg_res = ::SendMessage(npp_plugin::npp(), NPPM_MSGTOPLUGIN, (WPARAM)SOLUTIONHUB_DLL_FILE_NAME, (LPARAM)&comm);
		if(!msg_res)
			gbsr.result = -1;

		if(gbsr.result != SolutionHubResults::SH_NO_ERROR) {
			sprintf(temp_fail_buffer, "Failed to get buffersize for connections. Error(%d)", gbsr.result);
			show_fail_dialog(temp_fail_buffer);
			return;
		}

		resize_size = gsr.buffer_size*2;
		workbuffer.resize(resize_size);

		GetConnectionsRequest gcr; memset(&gcr, 0, sizeof(gcr));
		gcr.result = -2;

		gcr.connection_json = &workbuffer[0];
		gcr.buffer_size = resize_size;

		comm.internalMsg = NPPM_SOLUTIONHUB_CONFIG_GET_CONNECTIONS;
		comm.info = &gcr;
		msg_res = ::SendMessage(npp_plugin::npp(), NPPM_MSGTOPLUGIN, (WPARAM)SOLUTIONHUB_DLL_FILE_NAME, (LPARAM)&comm);
		if(!msg_res)
			gsr.result = -1;

		if(gcr.result != SolutionHubResults::SH_NO_ERROR) {
			sprintf(temp_fail_buffer, "Failed to get connections. Error(%d)", gsr.result);
			show_fail_dialog(temp_fail_buffer);
			return;
		}

		Json::Value got_connections;
		if(!json_aux::json_from_string(gcr.connection_json, got_connections)) {
			sprintf(temp_fail_buffer, "Failed to parse connections. Error(%d)", gcr.result);
			show_fail_dialog(temp_fail_buffer);
			return;
		}

		parse_solutions_from_settings(got_solutions, got_connections);
	}

	void parse_named_connections()
	{
		const wchar_t *self_name = npp_plugin::module_name();
		GetNamedConnectionsRequest gnc;

		gnc.connections = tempbuffers::buffer[0];
		gnc.buffer_size = tempbuffers::BUFFER_SIZE;

		CommunicationInfo comm;
		comm.internalMsg = NPPM_SOLUTIONHUB_CONFIG_GET_NAMED_CONNECTIONS;
		comm.srcModuleName = self_name;
		comm.info = &gnc;

		LRESULT msg_res = ::SendMessage(npp_plugin::npp(), NPPM_MSGTOPLUGIN, (WPARAM)SOLUTIONHUB_DLL_FILE_NAME, (LPARAM)&comm);

		if(msg_res) {
			if(gnc.result == SolutionHubResults::SH_NO_ERROR) {
				named_connections = tempbuffers::buffer[0];

				std::string connection_text = "Registered connections : [";
				connection_text.append(named_connections);
				connection_text.append("]");

				::SetWindowTextA(GetDlgItem(self, IDC_TEXT_CONNECTIONS), connection_text.c_str());
			}
		}
	}

	void delete_solution(const std::string &solution_name) {
		const wchar_t *self_name = npp_plugin::module_name();

		DeleteSolutionRequest dr;
		dr.solution_name = solution_name.c_str();

		CommunicationInfo comm;
		comm.internalMsg = NPPM_SOLUTIONHUB_CONFIG_DELETE_SOLUTION;
		comm.srcModuleName = self_name;
		comm.info = (void*)&dr;

		LRESULT msg_res = ::SendMessage(npp_plugin::npp(), NPPM_MSGTOPLUGIN, (WPARAM)SOLUTIONHUB_DLL_FILE_NAME, (LPARAM)&comm);

		if(msg_res && dr.result == SolutionHubResults::SH_NO_ERROR) {
			treelist::delete_selected(treelist_solutions);
			::SendMessage(self, WM_COMMAND, MAKEWPARAM((UINT)(IDC_BUTTON_SAVE_CONNECTIONS),(UINT)(0)), (LPARAM)(HWND)(0));
		} else {
			DEBUG_PRINT("[solutionhub_ui] Failed to delete solution!.");
		}
	}

	bool save_solution(const std::string &solution_name) {
		const Json::Value &s = solutions[solution_name];
		if(!s.isObject())
			return false;

		const wchar_t *self_name = npp_plugin::module_name();

		std::string t = json_aux::json_to_string(s);

		SaveSolutionRequest ssr;
		ssr.solution_name = solution_name.c_str();
		ssr.solution_json = t.c_str();

		CommunicationInfo comm;
		comm.internalMsg = NPPM_SOLUTIONHUB_CONFIG_SAVE_SOLUTION;
		comm.srcModuleName = self_name;
		comm.info = &ssr;

		LRESULT msg_res = ::SendMessage(npp_plugin::npp(), NPPM_MSGTOPLUGIN, (WPARAM)SOLUTIONHUB_DLL_FILE_NAME, (LPARAM)&comm);

		if(msg_res && ssr.result == SolutionHubResults::SH_NO_ERROR)
			return true;

		return false;
	}

	void save_connections(const Json::Value &c) {
		const wchar_t *self_name = npp_plugin::module_name();

		std::string t = json_aux::json_to_string(c);

		SaveConnectionsRequest scr;
		scr.connection_json = t.c_str();

		CommunicationInfo comm;
		comm.internalMsg = NPPM_SOLUTIONHUB_CONFIG_SAVE_CONNECTIONS;
		comm.srcModuleName = self_name;
		comm.info = (void*)&scr;

		LRESULT msg_res = ::SendMessage(npp_plugin::npp(), NPPM_MSGTOPLUGIN, (WPARAM)SOLUTIONHUB_DLL_FILE_NAME, (LPARAM)&comm);

		if(msg_res && scr.result == SolutionHubResults::SH_NO_ERROR) {
			parse_settings();
		}
	}

} // anonymous namespace

namespace {
	CResizer resizer;
	static CResizer::CBorderInfo s_borderinfo[] = {
		{IDC_GB_SOLUTION,
			{CResizer::eFixed,	IDC_MAIN, CResizer::eLeft},
			{CResizer::eFixed,	IDC_MAIN, CResizer::eTop},
			{CResizer::eFixed,	IDC_MAIN, CResizer::eRight},
			{CResizer::eFixed,	IDC_MAIN, CResizer::eBottom}
		},
	};

	void treelist_remove_selection(HWND h) {
		TreeView_SelectItem(h, 0);
	}

	void add_kv_pair(HWND h, const std::wstring &k, const std::wstring &v) {
		TVINSERTSTRUCT insert;
		insert.hParent = TVI_ROOT;
		insert.hInsertAfter = TVI_LAST;
		TV_ITEM &i = insert.item;
		memset(&i, 0, sizeof(i));
		i.mask = TVIF_TEXT | TVIF_SUBITEM;
		i.cChildren = 0; // column (for clarity)

		TV_ITEM sec_col; memset(&sec_col, 0, sizeof(sec_col));
		sec_col.mask = TVIF_SUBITEM | TVIF_TEXT;
		sec_col.cChildren = 1; // column

		i.pszText = (wchar_t*)k.c_str();

		HTREEITEM item = TreeList_InsertItem(h, &insert);
		sec_col.pszText = (wchar_t*)v.c_str();
		sec_col.hItem = item;

		TreeList_SetItem(h, &sec_col);
	}

	void treelist_solutions_fill() {
		HWND h = treelist_solutions;
		TreeList_DeleteAllItems(h);

		std::map<std::wstring, std::wstring> tempmap;

		std::vector<std::string> members = connections.getMemberNames();
		for(unsigned m=0; m < members.size();++m) {
			const std::string &key = members[m];
			if(connections[key].isArray()) {
				unsigned size = connections[key].size();
				if(size) {
					std::wstring connection_str(L"");
					for (unsigned i=0; i<size;++i) {
						connection_str += string_util::to_wide(connections[key][i].asCString());
						if(i != (size-1))
							connection_str += L",";
					}

					tempmap[string_util::to_wide(key.c_str())] = connection_str;
				}

			}
		}

		std::map<std::wstring, std::wstring>::const_iterator ti(tempmap.begin()), tend(tempmap.end());
		while(ti != tend) {
			treelist::add_kv_pair(h, ti->first.c_str(), ti->second.c_str());
			++ti;
		}

		std::map<std::wstring, Json::Value>::iterator i(solution_map.begin()), end(solution_map.end());
		while(i!=end) {
			const std::wstring &sn = (*i++).first;
			if(tempmap.end() == tempmap.find(sn))
				treelist::add_string_field(h, sn.c_str());
		}

		//treelist_templates_add_emptyfield(h);
	}

	void treelist_templates_add_selected_templatedata(HWND h);

	// folders

	void treelist_folders_fill(const Json::Value &folders) {
		if(folders.empty())
			return;

		HWND h = treelist_folders;
		TreeList_DeleteAllItems(h);

		unsigned size = folders.size();
		for(unsigned i=0; i < size; ++i) {
			const Json::Value &dir = folders[i];
			const char *path = dir["path"].asCString();
			std::wstring path_w = string_util::to_wide(path);

			const char *inc_filter = (dir.isMember("include_filter") ? dir["include_filter"].asCString() : 0);
			const char *exc_filter = (dir.isMember("exclude_filter") ? dir["exclude_filter"].asCString() : 0);

			bool recursive = (dir.isMember("recursive") ? dir["recursive"].asBool() : false);
			bool monitored = (dir.isMember("monitored") ? dir["monitored"].asBool() : false);

			TVINSERTSTRUCT insert;
			insert.hParent = TVI_ROOT;
			insert.hInsertAfter = TVI_LAST;
			TV_ITEM &ii = insert.item;
			memset(&ii, 0, sizeof(ii));
			ii.mask = TVIF_SUBITEM;
			ii.cChildren = 0; // column
			HTREEITEM item = TreeList_InsertItem(h, &insert);

			TV_ITEM ci = {0};
			ci.hItem = item;

			ci.mask = TVIF_SUBITEM | TVIF_TEXT | TVIF_STATE;
			ci.cChildren = 0;
			ci.pszText = (wchar_t*)path_w.c_str();
			ci.state = TVIS_STATEIMAGEMASK;
			TreeList_SetItem(h, &ci);
			if(inc_filter) {
				std::wstring inc_w = string_util::to_wide(inc_filter);
				ci.cChildren = 3;
				ci.pszText = (wchar_t*)inc_w.c_str();
				ci.stateMask = 1 << 12;
				TreeList_SetItem(h, &ci);
			}

			if(exc_filter) {
				std::wstring exc_w = string_util::to_wide(exc_filter);
				ci.cChildren = 3;
				ci.pszText = (wchar_t*)exc_w.c_str();
				ci.stateMask = 1 << 13;
				TreeList_SetItem(h, &ci);
			}

			if(!(exc_filter || inc_filter)) {
				//ci.mask = TVIF_SUBITEM | TVIF_STATE;
				ci.cChildren = 3; // column
				//ci.state = TVIS_STATEIMAGEMASK;
				ci.pszText = (wchar_t*)L"";
				ci.stateMask = 1 << 12;
				TreeList_SetItem(h, &ci);
			}
			ci.mask = TVIF_SUBITEM | TVIF_STATE;
			ci.cChildren = 1; // column
			ci.state = TVIS_STATEIMAGEMASK;
			ci.stateMask = 1 << (recursive ? 13 : 12); // enable checkbox(i.e. draw it, unchecked(12), checked(13))

			TreeList_SetItem(h, &ci);
			ci.cChildren = 2;
			ci.stateMask = 1 << (monitored ? 13 : 12);

			TreeList_SetItem(h, &ci);
		}
	}

	void treelist_folders_add_emptyfield(HWND h) {
		TVINSERTSTRUCT insert;
		insert.hParent = TVI_ROOT;
		insert.hInsertAfter = TVI_LAST;
		TV_ITEM &i = insert.item;
		memset(&i, 0, sizeof(i));
		i.mask = TVIF_SUBITEM;
		i.cChildren = 0; // column

		HTREEITEM item = TreeList_InsertItem(h, &insert);

		TV_ITEM cb_col; memset(&cb_col, 0, sizeof(cb_col));
		cb_col.mask = TVIF_SUBITEM | TVIF_STATE;
		cb_col.hItem = item;
		cb_col.cChildren = 1; // column
		cb_col.state = TVIS_STATEIMAGEMASK;
		// enable checkbox(i.e. draw it, unchecked(12), checked(13))
		cb_col.stateMask = 1 << 12;
		TreeList_SetItem(h, &cb_col);

		cb_col.cChildren = 2;
		TreeList_SetItem(h, &cb_col);

		cb_col.cChildren = 3;
		TreeList_SetItem(h, &cb_col);
		// ensure visible
		TreeList_EnsureVisible(h, item);
	}

	void treelist_templates_add_emptyfield(HWND h) {
		TVINSERTSTRUCT insert;
		insert.hParent = TVI_ROOT;
		insert.hInsertAfter = TVI_LAST;
		TV_ITEM &i = insert.item;
		memset(&i, 0, sizeof(i));
		i.mask = TVIF_SUBITEM;
		i.cChildren = 0; // column

		HTREEITEM item = TreeList_InsertItem(h, &insert);

		TV_ITEM sec_col; memset(&sec_col, 0, sizeof(sec_col));
		sec_col.mask = TVIF_SUBITEM;
		sec_col.hItem = item;
		sec_col.cChildren = 1; // column
		TreeList_SetItem(h, &sec_col);

		// ensure visible
		TreeList_EnsureVisible(h, item);
	}

	void treelist_templates_setup_columns() {
		LVCOLUMN col; memset(&col, 0, sizeof(col));
		//col.mask	= LVCF_WIDTH|LVCF_TEXT|LVCF_SUBITEM|LVCF_FMT;
		col.mask = TVCF_TEXT;
		col.cchTextMax = 256;
		col.pszText = L"Key";
		//col.cx = 100;

		TreeList_InsertColumn(treelist_templates, 0, &col);
		col.pszText = L"Value";
		TreeList_InsertColumn(treelist_templates, 1, &col);

		int mode = TVAE_EDIT|TVAE_FULLWIDTH;
		int res = TreeList_SetColumnAutoEdit(treelist_templates, 0, mode, 0);
		res = TreeList_SetColumnAutoEdit(treelist_templates, 1, mode, 0);
	}

	void treelist_solutions_setup_columns() {
		LVCOLUMN col; memset(&col, 0, sizeof(col));
		col.mask = TVCF_TEXT;
		LVCF_TEXT;
		col.cchTextMax = 256;
		wchar_t *scolumns[2] = { L"Solution Name", L"Connections" };
		for (int i = 0; i != 2; ++i) {
			col.pszText = scolumns[i];
			TreeList_InsertColumn(treelist_solutions, i, &col);
		}
	}

	void treelist_folders_setup_columns() {
		struct Cols {
			enum {
				PATH = 0,
				RECURSIVE,
				MONITORED,
				INCLUDE_FILTER,
				//EXCLUDE_FILTER,
				NUM_COLS
			};
		};

		//col.mask	= LVCF_WIDTH|LVCF_TEXT|LVCF_SUBITEM|LVCF_FMT;
		LVCOLUMN col; memset(&col, 0, sizeof(col));
		col.mask = TVCF_TEXT | LVCF_WIDTH;
		col.cchTextMax = 256;
		col.pszText = L"Path";
		col.cx = 350;
		TreeList_InsertColumn(treelist_folders, Cols::PATH, &col);

		col.pszText = L"Recursive";
		col.cx = 75;
		TreeList_InsertColumn(treelist_folders, Cols::RECURSIVE, &col);
		col.pszText = L"Monitored";
		col.cx = 75;
		TreeList_InsertColumn(treelist_folders, Cols::MONITORED, &col);

		col.cx = 450;
		col.pszText = L"File filter(checked == exclude)";
		TreeList_InsertColumn(treelist_folders, Cols::INCLUDE_FILTER, &col);
//		col.pszText = L"Exclude filter";
//		TreeList_InsertColumn(treelist_folders, Cols::EXCLUDE_FILTER, &col);

		int res;
		res = TreeList_SetColumnAutoEdit(treelist_folders, Cols::MONITORED, (TVAE_CHECK|TVAE_ICONCLICK)|~TVAE_EDIT, 0);
		res = TreeList_SetColumnAutoEdit(treelist_folders, Cols::RECURSIVE, (TVAE_CHECK|TVAE_ICONCLICK)|~TVAE_EDIT, 0);

		res = TreeList_SetColumnAutoEdit(treelist_folders, Cols::INCLUDE_FILTER, (TVAE_CHECK|TVAE_ICONCLICK)|(TVAE_EDIT|TVAE_FULLWIDTH), 0);
		//res = TreeList_SetColumnAutoEdit(treelist_folders, Cols::EXCLUDE_FILTER, TVAE_EDIT|TVAE_FULLWIDTH, 0);
		int mode = TVAE_ICONCLICK;
		res = TreeList_SetColumnAutoEdit(treelist_folders, Cols::PATH, mode, 0);
	}

	void set_combobox_template_names() {
		if(!combobox_templates)
			return;

		::SendMessage(combobox_templates, CB_SETCURSEL, (WPARAM)-1, 0);

		std::map<std::string, ProjectTemplate>::const_iterator i(project_templates.begin()), end(project_templates.end());
		while(i!=end) {
			const std::wstring name = string_util::to_wide((i->first).c_str());

			::SendMessage(combobox_templates, CB_ADDSTRING, 0, (LPARAM)name.c_str());
			++i;
		}
	}

	void clear_generic_data() {
		HWND h = treelist_templates;
		TreeList_DeleteAllItems(h);

		treelist_templates_add_selected_templatedata(combobox_templates);
		treelist_templates_add_emptyfield(h);
	}

	void clear_folder_data() {
		HWND h = treelist_folders;
		TreeList_DeleteAllItems(h);
		treelist_folders_add_emptyfield(h);
	}

	void clear_solution_data() {
		HWND h = treelist_solutions;
		TreeList_DeleteAllItems(h);
	}

	void clear_all_solution_data() {
		treelist_remove_selection(treelist_solutions);

		clear_folder_data();
		clear_generic_data();

		::SetWindowText(GetDlgItem(self, IDC_EDIT_SOLUTION_NAME), L"");
		::EnableWindow(::GetDlgItem(self, IDC_BUTTON_DELETE_SOLUTION), FALSE);
		::EnableWindow(::GetDlgItem(self, IDC_BUTTON_SOLUTION_REMOVE_DIRECTORY), FALSE);
		::EnableWindow(::GetDlgItem(self, IDC_BUTTON_REMOVE_TEMPLATEFIELD), FALSE);
	}

	void on_show() {
		if(self) {
			parse_template_defs();
			parse_settings();
			parse_named_connections();

			treelist_solutions_fill();
			clear_all_solution_data();

			set_combobox_template_names();

			ui_aux::center_window(self);
			::ShowWindow(self, SW_SHOW);
		}
	}

	void on_hide() {
		if(self) {
			::ShowWindow(self, SW_HIDE);
		}
	}

	void assign_project_template(const ProjectTemplate &pt) {
		HWND h = treelist_templates;
		Json::Value generic_attributes(Json::objectValue);

		const std::vector<std::string> &values = pt.values;
		unsigned num_values = (unsigned)values.size();

		bool changed = false;
		if(treelist::get_kv_pairs(h, generic_attributes, true)) {
			for(unsigned i=0; i < num_values; ++i) {
				const std::string &name = values[i];
				if(!generic_attributes.isMember(name)) {
					generic_attributes[name] = "";
					changed = true;
				}
			}
		} else {
			// empty
			for(unsigned i=0; i < num_values; ++i) {
				const std::string &name = values[i];
				generic_attributes[name] = "";
			}
			changed = true;
		}

		if(changed)
			treelist::set_kv_pairs(h, generic_attributes);
	}

	void treelist_templates_add_selected_templatedata(HWND h) {
		LRESULT index = ::SendMessage(h, CB_GETCURSEL, 0, 0);
		if(index != CB_ERR) {
			wchar_t buffer[256] = {};
			LRESULT len = ::SendMessage(h, CB_GETLBTEXT, (WPARAM)index, (LPARAM)buffer);
			if(len>0) {
				std::string s = string_util::from_wide(buffer);
				const ProjectTemplate &pt = project_templates[s];

				assign_project_template(pt);
			}
		}
	}

	void on_command_main(HWND hwnd, int id, HWND hwndCtl, UINT code) {
		if(id == IDCANCEL) {
			on_hide();
		} else if(id==IDC_BUTTON_NEW_SOLUTION) {
			clear_all_solution_data();
		} else if(id==IDC_BUTTON_DELETE_SOLUTION) {
			const wchar_t *body = L"Are you sure you want to delete selected solution ?";
			const wchar_t *title = L"SolutionHubUI - Delete solution";
			if(::MessageBox(hwnd, body, title, MB_YESNO | MB_ICONEXCLAMATION) == IDYES)
			{
				std::wstring sn;
				if(treelist::get_string_field(treelist_solutions, TreeList_GetSelection(treelist_solutions), 0, sn)) {
					DEBUG_PRINT("[solutionhub] Delete yes");
					std::string delete_sol = string_util::from_wide(sn.c_str());
					delete_solution(delete_sol);

					on_show(); // haxxor
				}
			} else {
				DEBUG_PRINT("[solutionhub] Delete no!");
			}

		} else if(id==IDC_BUTTON_SAVE_CONNECTIONS) {
			HWND h = treelist_solutions;
			std::string connection_validation = named_connections;

			Json::Value generic_attributes(Json::objectValue);
			Json::Value connections_to_save(Json::objectValue);

			bool failed = false;
			std::string fail_text("");

			if(treelist::get_kv_pairs(h, generic_attributes, false)) {
				std::vector<std::string> members = generic_attributes.getMemberNames();
				for(unsigned i=0; i < members.size() && !failed;++i) {
					const std::string &key = members[i];
					const std::string &lconnections = generic_attributes[key].asString();

					std::vector<std::string> tokens;
					unsigned N = string_util::tokenize(lconnections, ",.:;# ", tokens);
					if(N) {
						Json::Value array(Json::arrayValue);
						for(unsigned t = 0; t < N;++t) {
							const std::string &current = tokens[t];
							size_t fp = connection_validation.find(current);
							if(fp != std::string::npos) {
								connection_validation.erase(fp, current.length());
							} else {
								if(std::string::npos == named_connections.find(current)) {
									// No named connections like this
									failed = true;
									fail_text = "No connection is registered by the name of : ";
									fail_text += current;
								} else {
									// multiple connections
									failed = true;
									fail_text = "Multiple connections is not allowed(";
									fail_text += current;
									fail_text += ")";
								}
							}
							array.append(tokens[t]);
						}
						connections_to_save[key] = array;
					}
				}
			}

			if(connections_to_save.empty()) {
				DEBUG_PRINT("[solutionhub] No connections to save");
				connections_to_save = Json::Value(Json::objectValue);
			}
			#ifdef _DEBUG
				failed = false; // SUBMIT AND HEADS WILL ROLL!!
			#endif
			// TODO check here
			if(!failed)
				save_connections(connections_to_save);
			else {
				const char *body = fail_text.c_str();
				const char *title = "SolutionHubUI - Invalid connections";

				::MessageBoxA(hwnd, body, title, MB_OK | MB_ICONERROR);
			}
		} else if(id==IDC_BUTTON_ADD_SOLUTIONDIRECTORY) {
			treelist_folders_add_emptyfield(treelist_folders);
		} else if(id==IDC_BUTTON_SOLUTION_REMOVE_DIRECTORY) {
			treelist::delete_selected(treelist_folders);
			::EnableWindow(::GetDlgItem(self, IDC_BUTTON_SOLUTION_REMOVE_DIRECTORY), FALSE);

		} else if(id==IDC_BUTTON_REMOVE_TEMPLATEFIELD) {
			treelist::delete_selected(treelist_templates);
			::EnableWindow(::GetDlgItem(self, IDC_BUTTON_REMOVE_TEMPLATEFIELD), FALSE);
		} else if(id==IDC_BUTTON_OPENDIRECTORY) {
			std::wstring title(L"Please select a folder to be included in the solution...");
			std::wstring res;
			ui_aux::folder::open_browser(self, title, res);
		} else if(id==IDC_BUTTON_CLEAR_GENERIC) {
			clear_generic_data();
		} else if(id==IDC_COMBO_TEMPLATES) {
			if(code==CBN_SELCHANGE) {
				treelist_templates_add_selected_templatedata(hwndCtl);
			}
		} else if(id == IDOK) {
			DEBUG_PRINT("IDOK");
		} else if(id == IDC_BUTTON_SAVE_PROJECT) {
			HWND h = treelist_templates;

			Json::Value generic_attributes(Json::objectValue);
			treelist::get_kv_pairs(h, generic_attributes, false);

			char solution_name[256] = {};
			int snlen = ::GetWindowTextA(::GetDlgItem(self, IDC_EDIT_SOLUTION_NAME), solution_name, 256);

			h = treelist_folders;
			HTREEITEM i = TreeList_GetRoot(h);
			TV_ITEM item; memset(&item, 0, sizeof(item));
			item.mask = TVIF_TEXT | TVIF_TEXTPTR | TVIF_SUBITEM | TVIF_STATE;
			item.stateMask = TVIS_STATEIMAGEMASK;
			Json::Value folders(Json::arrayValue);

			bool aborted = false;

			while(i) {
				item.hItem = i;
				item.cChildren = 0;
				if(TreeList_GetItem(h, &item)) {
					bool recursive(false), monitored(false);
					wchar_t *folder(0), *incfilter(0), *exlfilter(0);

					folder = (item.cchTextMax ? item.pszText : 0);
					if(folder) {
						// double check...
						folder = (::PathIsDirectory(folder) ? folder : 0);
					}
					if(folder) {
						item.cChildren = 1;
						if(TreeList_GetItem(h, &item)) {
							recursive = (((item.state&TVIS_STATEIMAGEMASK)>>12)-1)==1;
						}

						item.cChildren = 2;
						if(TreeList_GetItem(h, &item)) {
							monitored = (((item.state&TVIS_STATEIMAGEMASK)>>12)-1)==1;
						}

						item.cChildren = 3;
						if(TreeList_GetItem(h, &item)) {

							bool exclude_filter = (((item.state&TVIS_STATEIMAGEMASK)>>12)-1)==1;
							if(exclude_filter)
								exlfilter = (item.cchTextMax ? item.pszText : 0);
							else
								incfilter = (item.cchTextMax ? item.pszText : 0);

						}

						Json::Value folder_info(Json::objectValue);
						folder_info["path"] = string_util::from_wide(folder);
						folder_info["recursive"] = recursive;
						folder_info["monitored"] = monitored;

						const wchar_t *checker = 0;
						if(incfilter)
						{
							checker = incfilter;
							folder_info["include_filter"] = string_util::from_wide(incfilter);
						}

						if(exlfilter)
						{
							checker = exlfilter;
							folder_info["exclude_filter"] = string_util::from_wide(exlfilter);
						}

						if(checker)
						{
							const char *allowed_non_characters = "._";
							bool confirmed = false;

							while(*checker && (aborted == confirmed))
							{
								if(!string_util::isletter(*checker))
								{
									if (strchr(allowed_non_characters, *checker) == 0)
									{
										const char *body = "Found a letter that is not '.' or '_' in the file filters. This might result in failed searches.\n\nThe recommended/standard way of separating extensions is like this : '.extension_a.extension_b.extension_c'.\nDo you wish to continue ?.";
										const char *title = "SolutionHubUI";
										aborted = ::MessageBoxA(hwnd, body, title, MB_YESNO | MB_ICONEXCLAMATION) != IDYES;
										confirmed = !aborted;
									}
								}
								++checker;
							}
						}

						folders.append(folder_info);
					}
				}

				i = (aborted ? 0 : TreeList_GetNextSibling(h, i));
			}
			// all parsed, check

			bool is_ok = !aborted && (snlen>0) && (!folders.empty() || !generic_attributes.empty());
			if(is_ok) {
				if(solutions.isMember(solution_name)) {
					// update
				}
				Json::Value &si = solutions[solution_name];
				si["directories"] = folders;
				si["attributes"] = generic_attributes;

				const bool save_ok = save_solution(solution_name);
				if(save_ok) {
					::SendMessage(self, WM_COMMAND, MAKEWPARAM((UINT)(IDC_BUTTON_SAVE_CONNECTIONS),(UINT)(0)), (LPARAM)(HWND)(0));
					on_show();
				}
			} else if(!aborted) {

				const wchar_t *body = L"Solution must have a name and at least one attribute or directory entry.";
				const wchar_t *title = L"SolutionHubUI - Solution invalid";

				::MessageBox(hwnd, body, title, MB_OK | MB_ICONERROR);
			}

		} else if(id == IDC_BUTTON_ADD_GENERIC_FIELD) {
			treelist_templates_add_emptyfield(treelist_templates);
		} else {
			return FORWARD_WM_COMMAND(hwnd, id, hwndCtl, code, ::DefWindowProc);
		}
	}

	void on_activate_main(HWND hwnd, UINT state, HWND hwndActDeact, BOOL fMinimized) {
		if (state == WA_INACTIVE) {
			static bool do_hide_on_loose_focus=false;
			if(do_hide_on_loose_focus)
				on_hide();
		} else {
			return FORWARD_WM_ACTIVATE(hwnd, state, hwndActDeact, fMinimized, ::DefWindowProc);
		}
	}

	void on_size_main(HWND hwnd, UINT state, int cx, int cy) {
		//resizer.Move();

		RECT r;
		resizer.GetDlgItemRect(IDC_TEMPLATE_FRAME, r);
		::SetWindowPos(treelist_templates, 0, r.left, r.top, r.right-r.left, r.bottom - r.top, SWP_NOCOPYBITS|SWP_NOZORDER);

		resizer.GetDlgItemRect(IDC_FOLDERS_FRAME, r);
		::SetWindowPos(treelist_folders, 0, r.left, r.top,r.right-r.left, r.bottom-r.top, SWP_NOCOPYBITS|SWP_NOZORDER);


		return FORWARD_WM_SIZE(hwnd, state, cx, cy, ::DefWindowProc);
	}

	BOOL on_initdialog_main(HWND hwnd, HWND hwndFocus, LPARAM lParam) {
		// setup resizer
		const int nSize = sizeof(s_borderinfo)/sizeof(s_borderinfo[0]);
		resizer.Init(hwnd, 0x0, s_borderinfo, nSize);

		// combobox
		combobox_templates = ::GetDlgItem(hwnd, IDC_COMBO_TEMPLATES);

		// treelist
		RECT xRect = {0};
		HWND treelist_parent = hwnd;

		::GetClientRect(treelist_parent, &xRect);
		::InflateRect(&xRect, 0, 0);

		DWORD def_style			= WS_VISIBLE | WS_CHILD;
		// TVS_EDITLABELS
		DWORD treelist_style	= /*TVS_LINESATROOT |*/ TVS_HASLINES/* | TVS_SHOWSELALWAYS*/ | TVS_DISABLEDRAGDROP | TVS_FULLROWSELECT | TVS_HASBUTTONS | TVS_EDITLABELS;
		DWORD treelist_templates_style = treelist_style;
		DWORD treelist_folders_style = TVS_HASLINES/* | TVS_SHOWSELALWAYS */| TVS_DISABLEDRAGDROP | TVS_FULLROWSELECT | TVS_HASBUTTONS | TVS_EDITLABELS;

		treelist_templates_style = def_style | treelist_templates_style;
		treelist_folders_style = def_style | treelist_folders_style;

		/*
		 *	Notes :
		 *		TVS_EDITLABELS		: enabling of editing columns
		 *		TVS_EX_SUBSELECT	: enabling selecting different columns
		 */

		DWORD treelist_templates_exstyle = TVS_EX_ITEMLINES | TVS_EX_ALTERNATECOLOR/* | TVS_EX_NOCOLUMNRESIZE*/ | TVS_EX_EDITCLICK | TVS_EX_SUBSELECT /*| 0x0004*/ | TVS_EX_GRAYEDDISABLE| TVS_EX_FULLROWMARK;
		//#define LVS_EX_DOUBLEBUFFER     0x00010000
		//#define TVS_EX_DOUBLEBUFFER         0x0004
		DWORD treelist_folders_exstyle = TVS_EX_ITEMLINES | TVS_EX_ALTERNATECOLOR/* | TVS_EX_NOCOLUMNRESIZE*/ | TVS_EX_EDITCLICK | TVS_EX_SUBSELECT |0x00010000 | TVS_EX_GRAYEDDISABLE| TVS_EX_FULLROWMARK;

		HINSTANCE instance = (HINSTANCE)npp_plugin::module();

		#if defined TVC_CLASSNAME
			#define TREE_CLASS_NAME TEXT("TreeList")
		#else
			#define TREE_CLASS_NAME TEXT("XLIST")
		#endif

		treelist_templates = CreateWindow(TREE_CLASS_NAME,
										L"",
										treelist_templates_style,
										xRect.left, xRect.top, xRect.right - xRect.left, xRect.bottom - xRect.top,
										treelist_parent,
										(HMENU)(UINT_PTR)TEMPLATE_TREELIST_ID,
										instance,
										0);

		treelist_folders = CreateWindow(TREE_CLASS_NAME,
										L"",
										treelist_folders_style,
										xRect.left, xRect.top, xRect.right - xRect.left, xRect.bottom - xRect.top,
										treelist_parent,
										(HMENU)(UINT_PTR)FOLDERS_TREELIST_ID,
										instance,
										0);

		DWORD treelist_solutions_style = treelist_folders_style;

		treelist_solutions = CreateWindow(TREE_CLASS_NAME,
										L"",
										treelist_solutions_style,
										xRect.left, xRect.top, xRect.right - xRect.left, xRect.bottom - xRect.top,
										treelist_parent,
										(HMENU)(UINT_PTR)SOLUTIONS_TREELIST_ID,
										instance,
										0);
		#undef TREE_CLASS_NAME

		assert(treelist_templates && treelist_folders && treelist_solutions);
		TreeList_SetExtendedStyle(treelist_templates, treelist_templates_exstyle);
		TreeList_SetExtendedStyle(treelist_folders, treelist_folders_exstyle);

		DWORD treelist_solutions_exstyle = TVS_EX_ITEMLINES | TVS_EX_ALTERNATECOLOR | TVS_EX_EDITCLICK | TVS_EX_SUBSELECT | TVS_EX_GRAYEDDISABLE | TVS_EX_FULLROWMARK;
		TreeList_SetExtendedStyle(treelist_solutions, treelist_solutions_exstyle);

		//! Columns
		treelist_templates_setup_columns();
		treelist_folders_setup_columns();
		treelist_solutions_setup_columns();

		// test insert
		treelist_templates_add_emptyfield(treelist_templates);
		treelist_folders_add_emptyfield(treelist_folders);

		RECT r;
		resizer.GetDlgItemRect(IDC_TEMPLATE_FRAME, r);
		::SetWindowPos(treelist_templates, 0, r.left, r.top, r.right-r.left, r.bottom - r.top, SWP_NOCOPYBITS|SWP_NOZORDER);

		resizer.GetDlgItemRect(IDC_FOLDERS_FRAME, r);
		::SetWindowPos(treelist_folders, 0, r.left, r.top,r.right-r.left, r.bottom-r.top, SWP_NOCOPYBITS|SWP_NOZORDER);

		resizer.GetDlgItemRect(IDC_FRAME_SOLUTIONS, r);
		::SetWindowPos(treelist_solutions, 0, r.left, r.top,r.right-r.left, r.bottom-r.top, SWP_NOCOPYBITS|SWP_NOZORDER);

		HINSTANCE hi = (HINSTANCE) npp_plugin::module();
		unsigned max_num_images = 16;
		unsigned flags = ILC_COLOR32 | ILC_MASK;

		image_list = ImageList_Create(16, 16, flags, 0, max_num_images);

		HICON hIcon = ::LoadIcon(hi, MAKEINTRESOURCE(IDI_KABINET));
		ImageList_AddIcon(image_list, hIcon);
		::DestroyIcon(hIcon);

		HIMAGELIST il = TreeList_SetImageList(treelist_folders, image_list, TVSIL_NORMAL);
		(void)il;
		return FORWARD_WM_INITDIALOG(hwnd, hwndFocus, lParam, ::DefWindowProc);
	}

	LRESULT on_notify_main(HWND hwnd, int idFrom, NMHDR *nmhdr) {
		UINT code = nmhdr->code;
		if(idFrom==(int)SOLUTIONS_TREELIST_ID) {
			if(code==TVN_SELCHANGED) {
				NMTREEVIEW *head = (NMTREEVIEW*)nmhdr;
				HWND h = head->hdr.hwndFrom;

				// solutions
				std::wstring sn;
				if(treelist::get_string_field(h, TreeList_GetSelection(h), 0, sn)) {
					Json::Value &sol_data = solution_map[sn];
					::SetWindowText(GetDlgItem(self, IDC_EDIT_SOLUTION_NAME), sn.c_str());

					if(sol_data["directories"].isArray() && !sol_data["directories"].empty()) {
						const Json::Value &directories = sol_data["directories"];
						treelist_folders_fill(directories);
					} else {
						//treelist_folders_add_emptyfield(treelist_folders);
					}

					if(sol_data["attributes"].isObject() && !sol_data["attributes"].empty()) {
						const Json::Value &attributes = sol_data["attributes"];
						treelist::set_kv_pairs(treelist_templates, attributes);
					} else {
						//treelist_templates_add_emptyfield(treelist_templates);
					}
				}
				::EnableWindow(::GetDlgItem(hwnd, IDC_BUTTON_DELETE_SOLUTION), TRUE);
				::EnableWindow(::GetDlgItem(self, IDC_BUTTON_SOLUTION_REMOVE_DIRECTORY), FALSE);
			} else if(code == TVN_STARTEDIT) {
				TV_STARTEDIT *se = (TV_STARTEDIT*)nmhdr;
				TV_ITEM &i = se->item;
				int column = i.cChildren;

				int return_res = (column == 0 ? 0 : 1);
#if _WIN64
				::SetWindowLong(hwnd, DWLP_MSGRESULT, return_res);
#else
				::SetWindowLong(hwnd, DWL_MSGRESULT, return_res);
#endif

				return (LRESULT) return_res;
			}
		}

		if(idFrom==TEMPLATE_TREELIST_ID)
		{
			if(code==TVN_KEYDOWN) {
				TV_KEYDOWN_EX &kd = *(TV_KEYDOWN_EX*)nmhdr;
				if(kd.wVKey == VK_DELETE) {
					treelist::delete_selected(treelist_templates);

					::EnableWindow(::GetDlgItem(self, IDC_BUTTON_REMOVE_TEMPLATEFIELD), FALSE);

				}
			} else if(code==TVN_SELCHANGING) {
				::EnableWindow(::GetDlgItem(self, IDC_BUTTON_REMOVE_TEMPLATEFIELD), TRUE);
			}
		}
		if(idFrom==FOLDERS_TREELIST_ID) {
			NMTREEVIEW *head = (NMTREEVIEW*)nmhdr;
			HWND h = head->hdr.hwndFrom;

			if(code==TVN_KEYDOWN) {
				TV_KEYDOWN_EX &kd = *(TV_KEYDOWN_EX*)nmhdr;
				if(kd.wVKey == VK_DELETE) {
					treelist::delete_selected(treelist_folders);

					::EnableWindow(::GetDlgItem(self, IDC_BUTTON_SOLUTION_REMOVE_DIRECTORY), FALSE);
				}

			} else if(code == NM_SETFOCUS) {
				DEBUG_PRINT("NM_SETFOCUS");
			} else if(code==TVN_SELCHANGING) {
				DEBUG_PRINT("TVN_SELCHANGING");
				::EnableWindow(::GetDlgItem(self, IDC_BUTTON_SOLUTION_REMOVE_DIRECTORY), TRUE);
			} else if(code == TVN_CBSTATECHANGED) {
				TV_ITEM item = head->itemNew;
				bool checked = (((item.state&TVIS_STATEIMAGEMASK)>>12)-1)==1;
				TreeView_SelectItem(h, 0);
				//::InvalidateRect(h, NULL, TRUE); // force redraw, change to real rect, must find TreeList_UpdateItem(...)

				DEBUG_PRINT("Selection changed, checked(%s)", (checked ? "true" : "false"));
				return (LRESULT) 1;
			} else if(code == TVN_STARTEDIT || code == NM_CLICK || code == NM_DBLCLK) {


				int return_res;
				int column;
				HTREEITEM tree_itemhandle = 0;
				if(code == NM_CLICK || code == NM_DBLCLK) {
					column = head->itemNew.cChildren;
					if(column == 0)
						tree_itemhandle = head->itemNew.hItem;

					return_res = (column == 0 ? 0 : 1);
				} else {
					TV_STARTEDIT *se = (TV_STARTEDIT*)nmhdr;
					TV_ITEM &i = se->item;
					column = i.cChildren;

					if(column == 0)
						tree_itemhandle = i.hItem;

					return_res = (column == 0 ? 0 : 1);
#if _WIN64
					::SetWindowLong(hwnd, DWLP_MSGRESULT, return_res);
#else
					::SetWindowLong(hwnd, DWL_MSGRESULT, return_res);
#endif
				}

				if(column == 0) {
					std::wstring title(L"Please select a folder to be included in solution...");
					std::wstring res;
					if(ui_aux::folder::open_browser(self, title, res)) {
						TV_ITEM cb_col; memset(&cb_col, 0, sizeof(cb_col));
						cb_col.mask = TVIF_SUBITEM | TVIF_TEXT;
						cb_col.hItem = tree_itemhandle;
						cb_col.cChildren = 0; // column
						cb_col.pszText = (wchar_t*)res.c_str();
						TreeList_SetItem(treelist_folders, &cb_col);
					}
				}

				return (LRESULT) return_res;
			}
		}

		return FORWARD_WM_NOTIFY(hwnd, idFrom, nmhdr, ::DefWindowProc);
	}

	void on_key_main(HWND hwnd, UINT vk, BOOL /*fDown*/, int cRepeat, UINT flags) {
		return FORWARD_WM_KEYDOWN( hwnd, vk, cRepeat, flags, ::DefWindowProc );
	}

	LRESULT solutionhub_message_proc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
		switch(message)	{
			HANDLE_MSG(hwnd,	WM_NOTIFY,			on_notify_main);
			HANDLE_MSG(hwnd,	WM_INITDIALOG,		on_initdialog_main);
			HANDLE_MSG(hwnd,	WM_COMMAND,			on_command_main);
			HANDLE_MSG(hwnd,	WM_ACTIVATE,		on_activate_main);
			HANDLE_MSG(hwnd,	WM_SIZE,			on_size_main);
			HANDLE_MSG(hwnd,	WM_KEYDOWN,			on_key_main);
			HANDLE_MSG(hwnd,	WM_KEYUP,			on_key_main);

			case WM_KILLFOCUS :
				{
					// wparam == Handle to the window that receives the keyboard focus. This parameter can be NULL.
	#if 0
					on_hide(); // FIX THIS!
	#endif
					return FALSE; // An application should return zero if it processes this message.
				}

			default:

				return FALSE;//::DefWindowProc( hwnd, message, wParam, lParam );
		}
	}
}

namespace npp_plugin_solutionhub_ui {
	void init()	{
		int res = TreeListRegister((HINSTANCE) npp_plugin::module());
		(void)res;
		HWND parent = npp_plugin::npp();

		self = ::CreateDialogParam((HINSTANCE) npp_plugin::module(),
									MAKEINTRESOURCE(IDD_SOLUTIONHUB),
									parent,
									(DLGPROC)solutionhub_message_proc,
									(LPARAM) 0x0
									);

		assert(self);
		::SendMessage(parent, NPPM_MODELESSDIALOG, MODELESSDIALOGADD, (LPARAM) self);
//		on_show();
	}

	void show()	{
		on_show();
	}

	void terminate() { }
}
