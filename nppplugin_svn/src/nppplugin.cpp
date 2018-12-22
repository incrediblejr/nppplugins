#include "npp/plugin/npp_plugin_interface.h"
#include "nppplugin_svn.h"

#ifdef DEMONSTRATE_SH_SEARCH
#include "npp_plugins/nppplugin_solutionhub_com_interface.h"
#endif

namespace npp_plugin {
	void about_func();
}

BOOL APIENTRY DllMain(HANDLE hModule, DWORD reason, LPVOID /*lpReserved*/)
{
	switch (reason)
	{

	case DLL_PROCESS_ATTACH:
		{
			npp_plugin::init(TEXT("Tortoise SVN"), hModule);
			npp_plugin::set_help_filename(TEXT("nppplugin_tsvn_help.txt"));
			npp_plugin::set_settings_filename(TEXT("nppplugin_tsvn.config"));

#ifdef DEMONSTRATE_SH_SEARCH
			npp_plugin::function_add(TEXT("Search"),					npp_plugin_svn::temp_search);
#endif

			npp_plugin::function_add(TEXT("TSVN - Project update"),		npp_plugin_svn::project_update);
			npp_plugin::function_add(TEXT("TSVN - Project commit"),		npp_plugin_svn::project_commit);
			npp_plugin::function_add(TEXT("TSVN - Project log"),		npp_plugin_svn::project_log);
			npp_plugin::function_add(TEXT(""), 0);
			npp_plugin::function_add(TEXT("TSVN - File diff"),			npp_plugin_svn::file_diff);
			npp_plugin::function_add(TEXT("TSVN - File log"),			npp_plugin_svn::file_log);
			npp_plugin::function_add(TEXT("TSVN - File commit"),		npp_plugin_svn::file_commit);
			npp_plugin::function_add(TEXT("TSVN - File update"),		npp_plugin_svn::file_update);
			npp_plugin::function_add(TEXT(""), 0);
			npp_plugin::function_add(TEXT("TSVN - File edit conflicts"),npp_plugin_svn::file_conflict_editor);
			npp_plugin::function_add(TEXT("TSVN - File resolve"),		npp_plugin_svn::file_resolve);
			npp_plugin::function_add(TEXT(""), 0);
			npp_plugin::function_add(TEXT("TSVN - File add"),			npp_plugin_svn::file_add);
			npp_plugin::function_add(TEXT("TSVN - File revert"),		npp_plugin_svn::file_revert);
			npp_plugin::function_add(TEXT("TSVN - File remove"),		npp_plugin_svn::file_remove);
			npp_plugin::function_add(TEXT("TSVN - File rename"),		npp_plugin_svn::file_rename);
			npp_plugin::function_add(TEXT(""), 0);
			npp_plugin::function_add(TEXT("TSVN - File lock"),			npp_plugin_svn::file_lock);
			npp_plugin::function_add(TEXT("TSVN - File unlock"),		npp_plugin_svn::file_unlock);
			npp_plugin::function_add(TEXT(""), 0);
			npp_plugin::function_add(TEXT("TSVN - File blame"),			npp_plugin_svn::file_blame);
			npp_plugin::function_add(TEXT(""), 0);
			npp_plugin::function_add(TEXT("TSVN - Help"),				npp_plugin_svn::open_helpfile);
			npp_plugin::function_add(TEXT("TSVN - Open config file"),	npp_plugin_svn::open_configfile);
			npp_plugin::function_add(TEXT("TSVN - About"),				npp_plugin::about_func);

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

				npp_plugin_svn::init();
			}
			break;

		case NPPN_SHUTDOWN:
			{
				npp_plugin_svn::terminate();
			}
			break;
		default:
			break;
	}
}

extern "C" __declspec(dllexport) LRESULT messageProc(UINT Message, WPARAM, LPARAM lParam)
{
	using namespace npp_plugin;

#if 0
	switch (Message)
	{
		case NPPM_MSGTOPLUGIN:
		{
			CommunicationInfo *comm = reinterpret_cast<CommunicationInfo *>(lParam);

			switch (comm->internalMsg)
			{
				case 42 :
					{
						#ifdef DEMONSTRATE_SH_SEARCH
							npp_plugin_svn::temp_on_searchresponse(comm->info);
						#endif
					}
				case NPPN_SOLUTIONHUB_CONNECTION_DELETED :
					{
						ConnectionChangeInfo &ci = *(ConnectionChangeInfo*)(comm->info);

						unsigned a = 1;
					}
					break;

				case NPPN_SOLUTIONHUB_CONNECTION_CHANGED :
					{
						ConnectionChangeInfo &ci = *(ConnectionChangeInfo*)comm->info;

						unsigned a = 1;
					}
					break;

				case NPPN_SOLUTIONHUB_CONNECTION_ADDED :
					{
						ConnectionChangeInfo &ci = *(ConnectionChangeInfo*)(comm->info);

						unsigned a = 1;
					}
					break;
				case NPPN_SOLUTIONHUB_SOLUTION_UPDATED :
					{
						SolutionChangeInfo &ci = *(SolutionChangeInfo*)(comm->info);

						unsigned a = 1;
					}
					break;

				case NPPN_SOLUTIONHUB_SOLUTION_DELETED :
					{
						SolutionChangeInfo &ci = *(SolutionChangeInfo*)(comm->info);

						unsigned a = 1;
					}
					break;
				default:
					break;
			}
			return TRUE;
		}
		default:
			return FALSE;

	}
#else
	(void)Message;
	(void)lParam;
	return FALSE;
#endif
}

namespace npp_plugin {
	void about_func()
	{
		const char *title = "TortoiseSVN (Version " PLUGIN_VERSION ")";
		const char *body = "Simple TortoiseSVN plugin for Notepad++.\n\n"
			"For latest information visit " REPO_PATH "\n\n"
			"Contact : " CONTACT_EMAIL
			"\n\n(Revision " REVISION_HASH ")";

		::MessageBoxA(npp_plugin::npp(), body, title, MB_OK);
	}
}

