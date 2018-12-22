#include "npp_plugin_interface.h"
#include <string>

#include <Shlwapi.h>
#pragma comment(lib, "Shlwapi.lib")

#ifdef NPPIF_CHECK_LOCALCONFIG
#include <shlobj.h>
#endif

#include <assert.h>

namespace {
	typedef std::wstring String;

	String plugin_name;
	String plugin_module_name;
	String plugin_module_path;
	String plugin_settings_path;

	String plugin_settings_file;
	String plugin_help_file;

	HANDLE module_handle;
	HWND npp_hwnd;
	HWND scintilla_hwnd_main;
	HWND scintilla_hwnd_secondary;
	HWND scintilla_hwnd_current;

	bool views_dirty;
	bool is_ready;

	#define NPP_PLUGIN_MAX_NUM_FUNCITEMS (128)
	FuncItem plugin_funcitems[NPP_PLUGIN_MAX_NUM_FUNCITEMS];
	int num_plugin_funcitems;

	HWND scintilla_views_update()
	{
		static HWND current_hwnd;
		views_dirty = false;

		int current;
		::SendMessage(npp_hwnd, NPPM_GETCURRENTSCINTILLA, 0, (LPARAM)&current);

		if (current != -1)
			current_hwnd = (current == MAIN_VIEW) ? scintilla_hwnd_main : scintilla_hwnd_secondary;

		return current_hwnd;
	}

	void init_settingspath()
	{
		#ifdef NPPIF_CHECK_LOCALCONFIG
			const wchar_t *local_configfile = L"doLocalConf.xml";

			String temp = plugin_module_path;
			temp += L"\\..\\";
			temp += local_configfile;

			// Test if localConf.xml exist
			bool isLocal = (PathFileExists(temp.c_str()) == TRUE);

			if(!isLocal)
			{
				ITEMIDLIST *pidl;
				SHGetSpecialFolderLocation(NULL, CSIDL_APPDATA, &pidl);
				TCHAR tmp[MAX_PATH];
				SHGetPathFromIDList(pidl, tmp);

				::PathAppend(tmp, L"\\Notepad++\\plugins\\config\\");

				if(::PathFileExists(tmp)) {
					plugin_settings_path = tmp;
					return;
				}
			}
		#endif
		plugin_settings_path = plugin_module_path + L"\\config\\";
	}
} // anonymous

namespace npp_plugin {

void init(const wchar_t *name, HANDLE hModule)
{
	assert(plugin_name.empty());
	plugin_name.assign(name);

	wchar_t temp[MAX_PATH];
	::GetModuleFileName((HMODULE)hModule, temp, MAX_PATH);

	plugin_module_path.assign(temp);
	plugin_module_path = plugin_module_path.substr(0, plugin_module_path.rfind(TEXT("\\")));

	init_settingspath();

	PathStripPath(temp);
	plugin_module_name.assign(temp);

	module_handle = hModule;
}

void set_settings_filename(const wchar_t *name)
{
	assert(!plugin_name.empty());
	plugin_settings_file = plugin_settings_path;
	plugin_settings_file.append(name);
}

void set_help_filename(const wchar_t *name)
{
	assert(!plugin_name.empty());

	plugin_help_file = plugin_module_path;
	plugin_help_file.append(L"\\Doc\\");
	plugin_help_file.append(name);
}

void function_add(const wchar_t *name, PFUNCPLUGINCMD func, ShortcutKey *shortcut/* = 0*/)
{
	assert(num_plugin_funcitems != NPP_PLUGIN_MAX_NUM_FUNCITEMS);
	if (num_plugin_funcitems == NPP_PLUGIN_MAX_NUM_FUNCITEMS)
		return;

	FuncItem &current = plugin_funcitems[num_plugin_funcitems++];

	size_t namelen = wcslen(name);
	memset(&current, 0, sizeof(current));
	if(namelen) {
		size_t maxlen = (sizeof(current._itemName) / sizeof(*current._itemName)) - 1;
		size_t len = namelen > maxlen ? maxlen : namelen;
		memcpy(current._itemName, name, len*sizeof(wchar_t));
		current._itemName[len] = 0;
	}

	current._cmdID = 0;
	current._pFunc = func;
	current._init2Check = false;
	current._pShKey = shortcut;
}

const wchar_t *module_name()		{ return plugin_module_name.c_str(); }
const wchar_t *settings_path()		{ return plugin_settings_path.c_str(); }

const wchar_t *module_path()		{ return plugin_module_path.c_str(); }
const wchar_t *settings_file()		{ return plugin_settings_file.c_str(); }
const wchar_t *help_file()			{ return plugin_help_file.c_str(); }

HANDLE module() { return module_handle; }
HWND npp() { return npp_hwnd; }

HWND scintilla_view_main() { return scintilla_hwnd_main; }
HWND scintilla_view_secondary() { return scintilla_hwnd_secondary; }

HWND scintilla_view_current()
{
	if (views_dirty || !is_ready)
		scintilla_hwnd_current = scintilla_views_update();

	return scintilla_hwnd_current;
}

HWND scintilla_view_other() { return (scintilla_view_current() == scintilla_hwnd_main) ? scintilla_hwnd_secondary : scintilla_hwnd_main; }
void scintilla_view_set_dirty() { views_dirty = true; }

bool ready() { return is_ready; }

void set_ready() { is_ready = true; }

}

extern "C" __declspec(dllexport) FuncItem *getFuncsArray(int *size)
{
	*size = num_plugin_funcitems;

	return plugin_funcitems;
}

extern "C" __declspec(dllexport) const TCHAR *getName() { return &plugin_name[0]; }

#ifdef UNICODE
extern "C" __declspec(dllexport) BOOL isUnicode() { return TRUE; }
#endif

extern "C" __declspec(dllexport) void setInfo(NppData data)
{
	npp_hwnd = data._nppHandle;
	scintilla_hwnd_main = data._scintillaMainHandle;
	scintilla_hwnd_secondary = data._scintillaSecondHandle;
}
