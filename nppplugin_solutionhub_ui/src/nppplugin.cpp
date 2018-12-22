#include "npp/plugin/npp_plugin_interface.h"
#include "nppplugin_solutionhub_ui.h"
#include "nppplugin_solutionhub_interface/nppplugin_solutionhub_com_interface.h"

namespace npp_plugin {
	void about_func();
}

enum SHUI_PLUGIN_COMMANDS {
	COMMAND_SHOW_UI = 0,

	COMMAND_NUM
};

static ShortcutKey shortcut_keys[COMMAND_NUM];

static void shortcut_init(ShortcutKey& s, bool ctrl, bool alt, bool shift, unsigned char key)
{
	s._isAlt = alt;
	s._isCtrl = ctrl;
	s._isShift = shift;
	s._key = key;
}

BOOL APIENTRY DllMain(HANDLE hModule, DWORD reason, LPVOID /*lpReserved*/)
{
	switch (reason)
	{

	case DLL_PROCESS_ATTACH:
		{
			shortcut_init(shortcut_keys[COMMAND_SHOW_UI], true, false, true, 0x4F);

			npp_plugin::init(TEXT("SolutionHubUI"), hModule);
			npp_plugin::set_help_filename(TEXT("nppplugin_solutionhub_help.txt"));

			#if 0
				npp_plugin::function_add(TEXT("SolutionHubUI - Show"), npp_plugin_solutionhub_ui::show, &shortcut_keys[COMMAND_SHOW_UI]);
			#else
				npp_plugin::function_add(TEXT("SolutionHubUI - Show"), npp_plugin_solutionhub_ui::show);
			#endif

			npp_plugin::function_add(TEXT(""), 0);
			npp_plugin::function_add(TEXT("SolutionHubUI - About"), npp_plugin::about_func);
		}
		break;

	case DLL_PROCESS_DETACH:
		break;

	case DLL_THREAD_ATTACH:
		break;

	case DLL_THREAD_DETACH:
		break;

	}

	return TRUE;
}

extern "C" __declspec(dllexport) void beNotified(SCNotification *notification)
{
	switch (notification->nmhdr.code)
	{
	case NPPN_READY:
		{
			npp_plugin::set_ready();
			npp_plugin::scintilla_view_set_dirty();

			npp_plugin_solutionhub_ui::init();
		}
		break;
	case NPPN_SHUTDOWN:
		{
			npp_plugin_solutionhub_ui::terminate();
		}
		break;
	default:
		break;
	}
}

extern "C" __declspec(dllexport) LRESULT messageProc(UINT, WPARAM, LPARAM)
{
	return TRUE;
}

namespace npp_plugin {
	void about_func()
	{
		const char *title = "SolutionHubUI (Version " PLUGIN_VERSION ")";
		const char *body =	"SolutionHubUI for use with SolutionHub plugin in Notepad++.\n\n"
			"For latest information visit " REPO_PATH "\n\n"
			"Contact : " CONTACT_EMAIL
			"\n\n(Revision " REVISION_HASH ")";

		::MessageBoxA(npp_plugin::npp(), body, title, MB_OK);
	}
 }
