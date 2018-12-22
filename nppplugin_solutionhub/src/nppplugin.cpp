#include "npp/plugin/npp_plugin_interface.h"
#include "nppplugin_solutionhub.h"
#include "nppplugin_solutionhub_com_interface.h"

namespace npp_plugin {
	void about_func();
}

BOOL APIENTRY DllMain(HANDLE hModule, DWORD reason, LPVOID /*lpReserved*/)
{
	switch (reason)
	{
	case DLL_PROCESS_ATTACH:
		{
			npp_plugin::init(TEXT("SolutionHub"), hModule);
			npp_plugin::set_help_filename(TEXT("nppplugin_solutionhub_help.txt"));
			npp_plugin::function_add(TEXT("SolutionHub - About"), npp_plugin::about_func);

			npp_plugin_solutionhub::init();
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
		}
		break;
	case NPPN_SHUTDOWN:
		{
			npp_plugin_solutionhub::terminate();
		}
		break;
	default:
		break;
	}
}

extern "C" __declspec(dllexport) LRESULT messageProc(UINT Message, WPARAM, LPARAM lParam)
{
	switch (Message)
	{

	case NPPM_MSGTOPLUGIN:
		{
			CommunicationInfo *comm = reinterpret_cast<CommunicationInfo *>(lParam);
			PluginMessage pm;
			pm.msg = (unsigned)comm->internalMsg;
			pm.src_module = comm->srcModuleName;
			pm.info = comm->info;
			npp_plugin_solutionhub::on_message(pm);
		}
		break;
	default:
		return false;
	}

	return TRUE;
}

namespace npp_plugin {
	void about_func()
	{
		const char *title = "SolutionHub (Version " PLUGIN_VERSION ")";
		const char *body =	"Provides indexing of solutions and communication for registered plugins.\n\n"
			"For latest information visit " REPO_PATH "\n\n"
			"Contact : " CONTACT_EMAIL
			"\n\n(Revision " REVISION_HASH ")";

		::MessageBoxA(npp_plugin::npp(), body, title, MB_OK);
	}
}
