#pragma once

namespace npp_plugin_st
{
	void init();
	void terminate();

	void open_settings();
	void reload_settings();

	void goto_file();
	void open_helpfile();

	void goto_file_line();
	void goto_file_line_clip();

	void switch_file();

	void on_searchresponse(void*);

	//!
	void on_dwellstart(int position, void *hwnd_from);
	void on_dwellend(int position, void *hwnd_from);
}
