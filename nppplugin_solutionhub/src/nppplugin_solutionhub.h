#pragma once

struct PluginMessage;

namespace npp_plugin_solutionhub {
	void init();
	void terminate();

	void on_message(const PluginMessage&);
}
