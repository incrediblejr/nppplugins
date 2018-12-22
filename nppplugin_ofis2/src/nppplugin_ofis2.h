#pragma once

namespace npp_plugin_ofis2
{
	void init();
	void init_ui();

	void terminate();

	void show_ui();

	void on_search_response(void*);

	void open_settings();
	void reload_settings();

	void open_helpfile();
}
