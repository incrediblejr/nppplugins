#pragma once

#include <windows.h>
#include "PluginInterface.h"

namespace npp_plugin {
	void init(const wchar_t *name, HANDLE hModule);
	void function_add(const wchar_t *name, PFUNCPLUGINCMD func, ShortcutKey *shortcut = 0);

	const wchar_t *module_name();
	const wchar_t *module_path();
	const wchar_t *settings_path();

	const wchar_t *settings_file();
	void set_settings_filename(const wchar_t *name);

	const wchar_t *help_file();
	void set_help_filename(const wchar_t *name);

	HANDLE module();
	HWND npp();

	HWND scintilla_view_main();
	HWND scintilla_view_secondary();

	void scintilla_view_set_dirty();

	HWND scintilla_view_current();
	HWND scintilla_view_other();

	void set_ready();
	bool ready();
}
