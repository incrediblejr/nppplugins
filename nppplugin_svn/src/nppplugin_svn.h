#pragma once

namespace npp_plugin_svn
{
	void init();
	void terminate();

	void project_update();
	void project_commit();
	void project_log();

	void file_diff();
	void file_log();
	void file_commit();
	void file_update();

	void file_conflict_editor();
	void file_add();
	void file_revert();
	void file_resolve();
	void file_remove();
	void file_rename();

	void file_lock();
	void file_unlock();

	void file_blame();

	void open_helpfile();
	void open_configfile();

	#ifdef DEMONSTRATE_SH_SEARCH
		void temp_on_searchresponse(void*);
		void temp_search();
	#endif

}
