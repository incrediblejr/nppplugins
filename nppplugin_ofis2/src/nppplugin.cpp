#include "npp/plugin/npp_plugin_interface.h"
#include "nppplugin_ofis2.h"
#include "nppplugin_ofis2_messages.h"

namespace npp_plugin {
	void about_func();
}

enum OFIS_PLUGIN_COMMANDS {
	COMMAND_SHOW = 0,

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
			npp_plugin::init(TEXT("Open File In Solution"), hModule);
			npp_plugin::set_help_filename(TEXT("nppplugin_ofis2_help.txt"));
			npp_plugin::set_settings_filename(TEXT("nppplugin_ofis2.config"));

			#if _DEBUG
				shortcut_init(shortcut_keys[COMMAND_SHOW], true, false, true, 0x4F);
				npp_plugin::function_add(TEXT("OFIS - Show"),		npp_plugin_ofis2::show_ui, &shortcut_keys[COMMAND_SHOW]);
			#else
				(void)&shortcut_init;
				npp_plugin::function_add(TEXT("OFIS - Show"),		npp_plugin_ofis2::show_ui);
			#endif

			npp_plugin::function_add(TEXT(""), 0);
			npp_plugin::function_add(TEXT("OFIS - Open settings"),		npp_plugin_ofis2::open_settings);
			npp_plugin::function_add(TEXT("OFIS - Reload settings"),	npp_plugin_ofis2::reload_settings);
			npp_plugin::function_add(TEXT(""), 0);
			npp_plugin::function_add(TEXT("OFIS - Help"),				npp_plugin_ofis2::open_helpfile);
			npp_plugin::function_add(TEXT("OFIS - About"),				npp_plugin::about_func);

			npp_plugin_ofis2::init();
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
	unsigned code = notification->nmhdr.code;
	switch (code)
	{
	case NPPN_READY:
		{
			npp_plugin::set_ready();
			npp_plugin::scintilla_view_set_dirty();

			npp_plugin_ofis2::init_ui();
		}
		break;
	case NPPN_SHUTDOWN:
		{
			npp_plugin_ofis2::terminate();
		}
		break;
	default:
		if(npp_plugin::ready())
		{
			if(code == NPPN_BUFFERACTIVATED)
				npp_plugin::scintilla_view_set_dirty();
		}
		break;
	}
}

extern "C" __declspec(dllexport) LRESULT messageProc(UINT Message, WPARAM , LPARAM lParam)
{
	switch (Message)
	{
		case NPPM_MSGTOPLUGIN:
		{
			CommunicationInfo* comm = reinterpret_cast<CommunicationInfo *>(lParam);

			switch (comm->internalMsg)
			{
				case NPPM_OFIS_ON_SEARCH_RESPONSE :
					{
						npp_plugin_ofis2::on_search_response(comm->info);

						return TRUE;
					}
					break;
				default:
					return FALSE;
			}
		}
		default:
			return FALSE;
	}
}

namespace npp_plugin {
	void about_func()
	{
		const char *title = "Open File In Solution (Version " PLUGIN_VERSION ")";
		const char *body = "Open File In Solution (V2) for Notepad++.\n\n"
			"For latest information visit " REPO_PATH "\n\n"
			"Contact : " CONTACT_EMAIL
			"\n\n(Revision " REVISION_HASH ")";
		::MessageBoxA(npp_plugin::npp(), body, title, MB_OK);
	}
}
