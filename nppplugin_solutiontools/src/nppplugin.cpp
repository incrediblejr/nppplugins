#include "npp/plugin/npp_plugin_interface.h"
#include "nppplugin_solutiontools.h"
#include "nppplugin_solutiontools_messages.h"

namespace npp_plugin {
	void about_func();
}

enum ST_PLUGIN_COMMANDS {
	COMMAND_GOTO = 0,

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
			shortcut_init(shortcut_keys[COMMAND_GOTO], true, false, false, 0x7B);

			npp_plugin::init(TEXT("Solution Tools"), hModule);
			npp_plugin::set_help_filename(TEXT("nppplugin_solutiontools_help.txt"));
			npp_plugin::set_settings_filename(TEXT("nppplugin_solutiontools.config"));

			#ifdef _DEBUG
				npp_plugin::function_add(TEXT("ST - Open document"),				npp_plugin_st::goto_file, &shortcut_keys[COMMAND_GOTO]);
			#else
				npp_plugin::function_add(TEXT("ST - Open document"),				npp_plugin_st::goto_file);
			#endif

			npp_plugin::function_add(TEXT("ST - Switch file"),			npp_plugin_st::switch_file);
			npp_plugin::function_add(TEXT(""), 0);
			npp_plugin::function_add(TEXT("ST - Goto file line"),		npp_plugin_st::goto_file_line);
			npp_plugin::function_add(TEXT("ST - Goto file line (clipboard)"),		npp_plugin_st::goto_file_line_clip);
			npp_plugin::function_add(TEXT(""), 0);

			npp_plugin::function_add(TEXT("ST - Open settings"),		npp_plugin_st::open_settings);
			npp_plugin::function_add(TEXT("ST - Reload settings"),		npp_plugin_st::reload_settings);
			npp_plugin::function_add(TEXT(""), 0);
			npp_plugin::function_add(TEXT("ST - Help"),				npp_plugin_st::open_helpfile);
			npp_plugin::function_add(TEXT("ST - About"),				npp_plugin::about_func);
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

extern "C" __declspec(dllexport) void beNotified(SCNotification *scnotification)
{
	const Sci_NotifyHeader &sciheader = scnotification->nmhdr;
	unsigned code = sciheader.code;

	switch (code)
	{
		case NPPN_READY:
			{
				npp_plugin::set_ready();
				npp_plugin::scintilla_view_set_dirty();

				npp_plugin_st::init();
			}
			break;
		case SCN_DWELLSTART :
			{
				npp_plugin_st::on_dwellstart(scnotification->position, sciheader.hwndFrom);
			}
			break;
		case SCN_DWELLEND :
			{
				npp_plugin_st::on_dwellend(scnotification->position, sciheader.hwndFrom);
			}
			break;
		case NPPN_SHUTDOWN:
			{
				npp_plugin_st::terminate();
			}
			break;
		default:
			if(npp_plugin::ready())
			{
				if(code == NPPN_BUFFERACTIVATED)
					npp_plugin::scintilla_view_set_dirty();
			}
	}
}

extern "C" __declspec(dllexport) LRESULT messageProc(UINT Message, WPARAM, LPARAM lParam)
{
	switch (Message)
	{
		case NPPM_MSGTOPLUGIN:
		{
			CommunicationInfo *comm = reinterpret_cast<CommunicationInfo *>(lParam);

			switch (comm->internalMsg)
			{
				case NPPM_ST_ON_SEARCH_RESPONSE :
					{
						npp_plugin_st::on_searchresponse(comm->info);
					}

				default:
					break;
			}
			return TRUE;
		}

		default:
			return FALSE;

	}
}

namespace npp_plugin {
	void about_func()
	{
		const char *title = "Solution Tools (Version " PLUGIN_VERSION ")";
		const char *body =	"Misc tools for Solutions.\n\n"
			"For latest information visit " REPO_PATH "\n\n"
			"Contact : " CONTACT_EMAIL
			"\n\n(Revision " REVISION_HASH ")";
		::MessageBoxA(npp_plugin::npp(), body, title, MB_OK);
	}
}
