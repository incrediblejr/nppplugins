#include "nppplugin_solutionhub.h"
#include "nppplugin_solutionhub_com_interface.h"

#include "npp/plugin/npp_plugin_interface.h"

#include "file_repository.h"
#include "filerecords.h"

#include "json_aux/json_aux.h"

#include <map>
#include <xutility> // min ?
#include <algorithm>
#include <functional>
#include <cctype> // isspace ?
#include "stream.h"
#include "string/string_utils.h"

#include "debug.h"

typedef std::wstring String;
#include <Shlwapi.h>
#include <shlobj.h>

namespace {
	template <class Key, class Value, class Comp, class Alloc>
	typename std::map<Key, Value, Comp, Alloc>::iterator get_key2(std::map<Key, Value, Comp, Alloc>& m, Value const& v)
	{
		typedef /*typename*/ std::map<Key, Value, Comp, Alloc> m_t;
		for (m_t::iterator i = m.begin(), end = m.end(); i != end; ++i)
		{
			if (i->second == v)
				return i;
		}
		return m.end();
	}

	template <class Key, class Value, class Comp, class Alloc>
	bool get_key(std::map<Key, Value, Comp, Alloc>& m, typename std::map<Key, Value, Comp, Alloc>::mapped_type const& v, typename std::map<Key, Value, Comp, Alloc>::key_type &k)
	{
		typedef /*typename*/ std::map<Key, Value, Comp, Alloc> m_t;
		for (m_t::iterator i = m.begin(), end = m.end(); i != end; ++i)
		{
			if (i->second == v)
			{
				k = i->first;
				return true;
			}
		}

		return false;
	}
}

namespace {
	String settings_base_path;

	String solution_settings_file;
	String connections_settings_file;

	bool init_failed;

	void set_basepath()
	{
		ITEMIDLIST *pidl;
		SHGetSpecialFolderLocation(NULL, CSIDL_APPDATA, &pidl);
		wchar_t tmp[MAX_PATH];
		SHGetPathFromIDList(pidl, tmp);
		String p(tmp);
		p.append(L"\\Notepad++\\plugins\\config\\");

		if(::PathFileExists(p.c_str())) {
			settings_base_path = p;
			return;
		}

		settings_base_path = npp_plugin::settings_path();
	}

	void init_settingsfile()
	{
		set_basepath();

		init_failed = false;
		solution_settings_file = L"";
		connections_settings_file = L"";

		String f = settings_base_path;

		bool file_exist = true;

		f.append(L"nppplugin_solutionhub_solutions.settings");
		if(!::PathFileExists( f.c_str() )) {
			HANDLE h = ::CreateFile(f.c_str(), GENERIC_WRITE, 0, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

			file_exist = (h != INVALID_HANDLE_VALUE);

			if (h == INVALID_HANDLE_VALUE)
				file_exist = false;

			::CloseHandle(h);
		}

		if(file_exist)
			solution_settings_file = f;

		//! Connections
		file_exist = true;

		f = settings_base_path;
		f.append(L"nppplugin_solutionhub_plugin_connections.settings");

		if(!::PathFileExists( f.c_str() )) {
			HANDLE h = ::CreateFile(f.c_str(), GENERIC_WRITE, 0, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

			file_exist = (h != INVALID_HANDLE_VALUE);

			if (h == INVALID_HANDLE_VALUE)
				file_exist = false;

			::CloseHandle(h);
		}

		if(file_exist)
			connections_settings_file = f;


		init_failed = (solution_settings_file.empty() || connections_settings_file.empty());

		if(!init_failed) {
			Json::Value settings_json;
			if(!json_aux::json_from_file(solution_settings_file, settings_json)) {
				Json::Value n(Json::objectValue);
				n["solutions"] = Json::Value(Json::objectValue);
				json_aux::json_to_file(solution_settings_file, n);
			}

			init_failed = !json_aux::json_from_file(solution_settings_file, settings_json);

			if(!init_failed) {
				Json::Value connection_json;
				if(!json_aux::json_from_file(connections_settings_file, connection_json)) {
					Json::Value n(Json::objectValue);
					n["solution"] = Json::Value(Json::objectValue);
					n["connection"] = Json::Value(Json::objectValue);
					json_aux::json_to_file(connections_settings_file, n);
				}

				init_failed = !json_aux::json_from_file(connections_settings_file, connection_json);
			}
		}
	}

	struct SearchWrapper {
		wchar_t *plugin;
		unsigned int response_code;

		char *userdata;
		unsigned userdata_size;
	};

	SearchWrapper *searchwrapper_make(const wchar_t *p, unsigned code, void *userdata, unsigned userdata_size)
	{
		unsigned l = (unsigned)wcslen(p);
		SearchWrapper *sw = new SearchWrapper; memset(sw, 0, sizeof(*sw));
		sw->plugin = new wchar_t[l+1];
		memcpy(sw->plugin, p, sizeof(wchar_t)*l);
		sw->plugin[l] = 0;
		sw->response_code = code;

		if(userdata_size) {
			sw->userdata_size = userdata_size;
			sw->userdata = new char[userdata_size];
			memcpy(sw->userdata, userdata, userdata_size);
		}
		return sw;
	}

	void searchwrapper_delete(SearchWrapper *&sw)
	{
		delete [] sw->plugin;
		delete [] sw->userdata;

		delete sw;
		sw = 0;
	}

	void notify_searchresponse(const SearchWrapper *sw, void *buffer, unsigned buffersize)
	{
		const wchar_t *plugin = sw->plugin;
		long internal_msg = sw->response_code;
		void *userdata = sw->userdata;
		unsigned userdata_size = sw->userdata_size;

		SearchResponse sr; memset(&sr, 0, sizeof(sr));
		sr.data = (const char*) buffer;
		sr.data_size = buffersize;
		sr.userdata = userdata;
		sr.userdata_size = userdata_size;

		CommunicationInfo comm;
		comm.internalMsg = internal_msg;
		comm.srcModuleName = npp_plugin::module_name();
		comm.info = &sr;

		::SendMessage(npp_plugin::npp(), NPPM_MSGTOPLUGIN, (WPARAM)plugin, (LPARAM)&comm);
	}

	void filerepo_search_callback(void *userdata, void *buffer, unsigned buffersize)
	{
		SearchWrapper *sw = (SearchWrapper *)userdata;
		notify_searchresponse(sw, buffer, buffersize);
		searchwrapper_delete(sw);
	}

	/*
	 *	For future reference, in case I forget once again.
	 *
	 *	Alias was what I called "registered" plugin -> solution.
	 *	Ex. Every plugin hook to SolutionHub with an alias.
	 *	Later when plugin commands is received I look up
	 *	the "plugin_name"->"alias" and then "alias" -> "solution"
	 */

	std::map<std::string, FileRepositoryHandle> solution_to_repo_map;
	std::map<std::wstring, std::string> receiver_to_alias;
	std::map<std::string, std::string> alias_to_solutionname;	// Parse me, on load and then on save again...

	//! Indexed aliases is plugins thats needs indexing (ex. ofis)
	typedef std::pair<bool, std::string> IndexPair;
	std::map<std::string, IndexPair> indexed_aliases;

	bool get_settings(Json::Value &r)
	{
		Json::Value settings;
		if(!json_aux::json_from_file(solution_settings_file, settings)) {
			r = Json::Value(Json::nullValue);
			return false;
		}

		r = settings;
		return true;
	}

	bool get_solutions(Json::Value &r)
	{
		Json::Value settings;
		if(!get_settings(settings)) {
			r = Json::Value(Json::nullValue);
			return false;
		}

		r = settings["solutions"]; // will get null if not present
		return r.isObject();
	}

	bool get_connections(Json::Value &r)
	{
		Json::Value settings;
		if(!json_aux::json_from_file(connections_settings_file, settings)) {
			return false;
		}

		if(!settings.isMember("solution"))
			return false;

		r = settings["solution"];

		return r.isObject();
	}

	void send_notification(const wchar_t *toplugin, unsigned type, void *data)
	{
		CommunicationInfo comm;
		comm.internalMsg = type;
		comm.srcModuleName = npp_plugin::module_name();
		comm.info = data;

		::SendMessage(npp_plugin::npp(), NPPM_MSGTOPLUGIN, (WPARAM)toplugin, (LPARAM)&comm);
	}

	void setup_alias_mappings()
	{
		Json::Value settings;
		if(!json_aux::json_from_file(connections_settings_file, settings))
			return;

		if(!settings.isMember("connection"))
			return;

		const Json::Value &alias_map = settings["connection"];
		if(!alias_map.isObject())
			return;

		std::map<std::string, std::string> temp;

		std::vector<std::string> members = alias_map.getMemberNames();

		for(unsigned i=0, s=(unsigned)members.size(); i < s ; ++i) {
			const std::string &alias = members[i];
			const std::string &solution = alias_map[alias].asString();

			temp[alias] = solution;
		}

		using namespace npp;

		std::vector<char> notifications;
		std::wstring receiver;

		// Map String, String
		typedef std::map<std::string, std::string>::const_iterator MSS_CIter;
		typedef std::map<std::string, std::string>::iterator MSS_Iter;
		MSS_Iter i(alias_to_solutionname.begin());

		while(i != alias_to_solutionname.end())
		{
			const char *alias = i->first.c_str();
			const char *solution = i->second.c_str();
			MSS_CIter f = temp.find(alias);

			if(f == temp.end())
			{
				// notify deletion of connection
				if(strlen(solution) > 0)
				{
					DEBUG_PRINT("Deleted connection for alias(%s), solution(%s)", alias, solution);
					if(get_key(receiver_to_alias, i->first, receiver))
					{
						stream::pack(notifications, (unsigned)(NPPN_SOLUTIONHUB_CONNECTION_DELETED));
						stream::pack_string_wide(notifications, receiver.c_str());
						stream::pack_string(notifications, solution);
					}
				}
				i = alias_to_solutionname.erase(i);
			}
			else
			{
				if(f->second != solution)
				{
					const char *new_solution = f->second.c_str();
					DEBUG_PRINT("Changed solution for alias(%s), from(%s), to(%s)", alias, solution, new_solution);
					// notify change
					if(get_key(receiver_to_alias, i->first, receiver))
					{
						stream::pack(notifications, (unsigned)(NPPN_SOLUTIONHUB_CONNECTION_CHANGED));
						stream::pack_string_wide(notifications, receiver.c_str());
						stream::pack_string(notifications, solution);
						stream::pack_string(notifications, new_solution);
					}
					i->second = f->second;
				}

				temp.erase(f);
				++i;
			}
		}

		i = temp.begin();
		while(i!=temp.end()) {
			const char *alias = i->first.c_str();
			const char *solution = i->second.c_str();
			alias_to_solutionname.insert(std::make_pair(alias, solution));

			if(get_key(receiver_to_alias, i->first, receiver))
			{
				stream::pack(notifications, (unsigned)(NPPN_SOLUTIONHUB_CONNECTION_ADDED));
				stream::pack_string_wide(notifications, receiver.c_str());
				stream::pack_string(notifications, solution);
			}
			DEBUG_PRINT("New connection, from alias(%s) to solution(%s)", alias, solution);
			++i;
		}

		DEBUG_PRINT("## END ##");
		unsigned N = (unsigned)notifications.size();
		const char *b = (N ? &notifications[0] : 0);

		while(N)
		{
			unsigned type = stream::unpack<unsigned>(b);

			unsigned len = stream::unpack<unsigned>(b);
			const wchar_t *plugin = (const wchar_t*)b;
			stream::advance(b, len*sizeof(wchar_t));

			unsigned slen = stream::unpack<unsigned>(b);
			const char *solution = b;
			stream::advance(b, slen*sizeof(char));

			unsigned consume = (3*sizeof(unsigned))+(len*sizeof(wchar_t)) + (slen*sizeof(char));

			if(type == NPPN_SOLUTIONHUB_CONNECTION_DELETED)
			{
				ConnectionChangeInfo info = { 0, solution };
				send_notification(plugin, type, &info);
			}
			else if(type == NPPN_SOLUTIONHUB_CONNECTION_CHANGED)
			{
				unsigned tolen = stream::unpack<unsigned>(b);
				const char *tosolution = b;
				stream::advance(b, tolen*sizeof(char));

				consume += sizeof(unsigned) + tolen*sizeof(char);

				ConnectionChangeInfo info = { tosolution, solution };
				send_notification(plugin, type, &info);
			}
			else if(type == NPPN_SOLUTIONHUB_CONNECTION_ADDED)
			{
				ConnectionChangeInfo info = { solution, 0 };
				send_notification(plugin, type, &info);
			}

			N -= consume;
		}
	}

	void save_connections(const Json::Value &newconnections)
	{
		Json::Value solution_alias_map(Json::objectValue);
		solution_alias_map["solution"] = newconnections;
		solution_alias_map["connection"] = Json::Value(Json::objectValue);

		std::vector<std::string> members = newconnections.getMemberNames();

		for(unsigned i=0; i < members.size(); ++i)
		{
			const std::string &solution_name = members[i];
			if(newconnections[solution_name].isArray())
			{
				unsigned size = newconnections[solution_name].size();
				for(unsigned j=0;j<size;++j)
				{
					std::string alias_name = newconnections[solution_name][j].asString();
					if(!solution_alias_map["connection"][alias_name])
						solution_alias_map["connection"][alias_name] = solution_name;
				}
			}
		}

		json_aux::json_to_file(connections_settings_file, solution_alias_map);

		setup_alias_mappings();
	}

	bool save_solutions(const Json::Value &s)
	{
		Json::Value settings;
		if(!get_settings(settings))
			return false;

		settings["solutions"] = s;

		json_aux::json_to_file(solution_settings_file, settings);

		return true;
	}

	bool delete_solution(const char *name)
	{
		Json::Value sol;
		if(get_settings(sol))
		{
			if(sol["solutions"].isObject())
			{
				sol["solutions"].removeMember(name);

				//! NTF :	Should check connections also ?.
				//			As of now I will save them after delete so it works.
				FileRepositoryHandle handle = solution_to_repo_map[name];
				if(handle)
				{
					filerepo::stop(handle);
					solution_to_repo_map[name] = 0;
				}

				json_aux::json_to_file(solution_settings_file, sol);

				return true;
			}
		}

		return false;
	}

	std::string get_solutionname_by_alias(const char *alias)
	{
		return alias_to_solutionname[alias];
	}

	Json::Value get_named_solution(const std::string &sn)
	{
		Json::Value solutions(Json::objectValue);
		get_solutions(solutions);
		if(!solutions.isObject())
			return Json::Value(Json::nullValue);

		return solutions[sn];
	}

	bool get_attributes(const std::string &sn, Json::Value &res)
	{
		Json::Value s = get_named_solution(sn);
		if(!s.isObject())
			return false;

		const Json::Value &attributes = s["attributes"];
		if(!attributes.isObject())
			return false;

		res = attributes;

		return true;
	}

	std::string get_named_attribute(const std::string &sn, const std::string &a)
	{
		Json::Value attributes;
		if(!get_attributes(sn, attributes))
			return "";

		return (attributes[a].isString() ? attributes[a].asString() : "");
	}

	bool index_solution(const std::string &sn)
	{
		Json::Value sol;
		if(!get_solutions(sol))
			return false;

		std::vector<std::string> members = sol.getMemberNames();
		for(unsigned i=0; i<members.size(); ++i)
		{
			const std::string solution_name = members[i];
			if(solution_name.compare(sn)==0)
			{
				Json::Value const &solution = sol[solution_name];
				Json::Value const &directories = solution["directories"];
				if(directories.isArray())
				{
					FileRepositoryHandle handle = solution_to_repo_map[solution_name];
					if(!handle)
					{
						handle = filerepo::allocate_handle();
						filerepo::add_solution(handle, solution);
						solution_to_repo_map[solution_name] = handle;
					}
				}
				return true;
			}
		}

		return false;
	}

	std::string get_named_connections()
	{
		std::string res("");

		std::map<std::wstring, std::string>::const_iterator i(receiver_to_alias.begin()), end(receiver_to_alias.end());
		while(i!=end)
		{
			const std::string &alias = i->second;
			if(res.find(alias) == std::string::npos)
			{
				res += alias;
				res += " ";
			}
			++i;
		}
		// trim trailing whites
		res.erase(std::find_if(res.rbegin(), res.rend(), std::not1(std::ptr_fun<int, int>(std::isspace))).base(), res.end());
		return res;
	}

	void check_solution_indexing()
	{
		std::map<std::string, IndexPair>::iterator i(indexed_aliases.begin()), end(indexed_aliases.end());
		while(i!=end)
		{
			IndexPair &index_pair = i->second;
			std::string &bound_sn = index_pair.second;

			std::string const &sn = alias_to_solutionname[i->first];
			if(sn.empty())
			{
				if(!bound_sn.empty())
				{
					FileRepositoryHandle h = solution_to_repo_map[ bound_sn ];
					if(h)
					{
						filerepo::stop(h);
						solution_to_repo_map[ bound_sn ] = 0;
					}
					index_pair.second = "";
				}
			}
			else
			{
				// solution is NOT empty
				if(bound_sn.empty())
				{
					index_pair.second = (index_solution(sn) ? sn : "");
				}
				else
				{
					if(bound_sn.compare(sn) != 0)
					{
						FileRepositoryHandle h = solution_to_repo_map[bound_sn];
						if(h)
						{
							filerepo::stop(h);
							solution_to_repo_map[bound_sn] = 0;
						}
					}
					index_pair.second = (index_solution(sn) ? sn : "");
				}
			}

			++i;
		}
	}

	void on_solution_saved(const std::string &solution_name)
	{
		std::map<std::string, IndexPair>::iterator i(indexed_aliases.begin()), end(indexed_aliases.end());
		while(i!=end)
		{
			IndexPair &index_pair = i->second;
			std::string &bound_sn = index_pair.second;
			if(bound_sn.compare(solution_name) == 0)
			{
				FileRepositoryHandle handle = solution_to_repo_map[bound_sn];
				if(handle)
				{
					filerepo::stop(handle);
					solution_to_repo_map[bound_sn] = 0;
				}

				index_solution(solution_name); // do all the work again
			}
			++i;
		}
	}

	void notify_solution_update(const char *solution_name, unsigned type, const char *solution_json)
	{
		const std::string sn(solution_name);

		std::string bound_alias;
		if(get_key(alias_to_solutionname, sn, bound_alias))
		{
			String plugin;
			if(get_key(receiver_to_alias, bound_alias, plugin))
			{
				SolutionChangeInfo info = { solution_name, solution_json };

				send_notification(plugin.c_str(), type, &info);
			}
		}
	}

	void on_config_message(long msg, const wchar_t *plugin, void *info)
	{
		if(msg == NPPM_SOLUTIONHUB_CONFIG_GET_SOLUTIONS)
		{
			GetSolutionsRequest &gsr = *(GetSolutionsRequest*)(info);
			Json::Value solutions;
			if(!get_solutions(solutions)) {
				gsr.result = SolutionHubResults::SH_ERROR;
				return;
			}

			std::string t = json_aux::json_to_string(solutions);
			if(gsr.buffer_size > t.length()) {
				strcpy(gsr.solution_json, t.c_str());
				gsr.result = SolutionHubResults::SH_NO_ERROR;
			} else {
				gsr.result = SolutionHubResults::SH_ERROR_BUFFER_TO_SMALL;
			}

		}
		else if(msg == NPPM_SOLUTIONHUB_CONFIG_GET_CONNECTIONS)
		{
			GetConnectionsRequest &gcr = *((GetConnectionsRequest*)(info));

			Json::Value connections;
			if(!get_connections(connections)) {
				gcr.result = SolutionHubResults::SH_ERROR;
				return;
			}

			std::string t = json_aux::json_to_string(connections);
			if(gcr.buffer_size > t.length()) {
				strcpy(gcr.connection_json, t.c_str());
				gcr.result = SolutionHubResults::SH_NO_ERROR;
			} else {
				gcr.result = SolutionHubResults::SH_ERROR_BUFFER_TO_SMALL;
			}

		}
		else if(msg == NPPM_SOLUTIONHUB_CONFIG_GET_NAMED_CONNECTIONS)
		{
			GetNamedConnectionsRequest &ncr = *((GetNamedConnectionsRequest*)info);

			std::string connections = get_named_connections();
			unsigned L = (unsigned)connections.length();

			if(L > ncr.buffer_size) {
				ncr.result = SolutionHubResults::SH_ERROR_BUFFER_TO_SMALL;
				return;
			}

			char *dest = ncr.connections;
			if(!L)
				*dest = 0;
			else
				strcpy(dest, connections.c_str());

			ncr.result = SolutionHubResults::SH_NO_ERROR;
		}
		else if(msg == NPPM_SOLUTIONHUB_CONFIG_DELETE_SOLUTION)
		{
			DeleteSolutionRequest &dr = *((DeleteSolutionRequest*)info);
			dr.result = SolutionHubResults::SH_ERROR;

			const char *sol_name = dr.solution_name;
			if(delete_solution(sol_name))
			{
				dr.result = SolutionHubResults::SH_NO_ERROR;
				notify_solution_update(sol_name, NPPN_SOLUTIONHUB_SOLUTION_DELETED, 0);
			}
		}
		else if(msg == NPPM_SOLUTIONHUB_CONFIG_SAVE_CONNECTIONS)
		{
			SaveConnectionsRequest &scr = *((SaveConnectionsRequest*)info);
			Json::Value c;
			if(!json_aux::json_from_string(scr.connection_json, c)) {
				scr.result = SolutionHubResults::SH_ERROR_INVALID_JSON;
				return;
			}

			save_connections(c);
			check_solution_indexing();
			scr.result = SolutionHubResults::SH_NO_ERROR;
		}
		else if(msg == NPPM_SOLUTIONHUB_CONFIG_SAVE_SOLUTION)
		{
			SaveSolutionRequest &ssr = *((SaveSolutionRequest*)info);
			Json::Value s;
			if(!json_aux::json_from_string(ssr.solution_json, s))
			{
				ssr.result = SolutionHubResults::SH_ERROR_INVALID_JSON;
				return;
			}

			ssr.result = SolutionHubResults::SH_ERROR;

			const std::string sn(ssr.solution_name);

			Json::Value solutions;
			if(get_solutions(solutions))
			{
				solutions[sn] = s;
				if(save_solutions(solutions))
				{
					on_solution_saved(sn);
					ssr.result = SolutionHubResults::SH_NO_ERROR;
					//! NOTIFICATION HERE
					notify_solution_update(ssr.solution_name, NPPN_SOLUTIONHUB_SOLUTION_UPDATED, ssr.solution_json);
				}
			}
		}
		else if(msg == NPPM_SOLUTIONHUB_CONFIG_UPDATE_ATTRIBUTES ||
				msg == NPPM_SOLUTIONHUB_CONFIG_UPDATE_ATTRIBUTES_HOOKED)
		{
			UpdateAttributesRequest &uar = *((UpdateAttributesRequest*)info);

			std::string solution_name;
			if(msg == NPPM_SOLUTIONHUB_CONFIG_UPDATE_ATTRIBUTES) {
				solution_name = uar.solution_name;
			} else {
				const std::string &alias = receiver_to_alias[plugin];
				if(!alias.empty())
					solution_name = get_solutionname_by_alias(alias.c_str());
			}

			if(solution_name.empty()) {
				uar.result = SolutionHubResults::SH_ERROR_NO_SOLUTION;
				return;
			}

			Json::Value new_attributes;
			if(!json_aux::json_from_string(uar.attribute_json, new_attributes)) {
				uar.result = SolutionHubResults::SH_ERROR_INVALID_JSON;
				return;
			}

			Json::Value solutions;
			if(!get_solutions(solutions)) {
				uar.result = SolutionHubResults::SH_ERROR;
				return;
			}

			Json::Value &current_solution = solutions[solution_name];
			if(!current_solution.isObject())
			{
				uar.result = SolutionHubResults::SH_ERROR_NO_SOLUTION;
				return;
			}

			Json::Value &current_attributes = current_solution["attributes"];
			if(!current_attributes.isObject())
			{
				current_solution["attributes"] = Json::Value(Json::objectValue);
				current_attributes = current_solution["attributes"];
			}

			std::vector<std::string> members = new_attributes.getMemberNames();
			for(unsigned i=0, n=(unsigned)members.size(); i != n; ++i) {
				const std::string key = members[i];
				const Json::Value &val = new_attributes[key];

				current_attributes[key] = val;
			}

			bool res = save_solutions(solutions);
			uar.result = (res ? SolutionHubResults::SH_NO_ERROR : SolutionHubResults::SH_ERROR);

			if(res)
			{
				const char *solname = solution_name.c_str();

				std::string t = json_aux::json_to_string(current_solution);
				const char *solution_json = t.c_str();

				notify_solution_update(solname, NPPN_SOLUTIONHUB_SOLUTION_UPDATED, solution_json);
			}
		}
		else if(msg == NPPM_SOLUTIONHUB_CONFIG_GET_SOLUTIONS_BUFFERSIZE)
		{
			GetBufferSizeRequest &gbsr = *((GetBufferSizeRequest*)info);
			Json::Value val;
			if(!get_solutions(val)) {
				gbsr.result = SolutionHubResults::SH_ERROR;
				return;
			}

			const std::string sol_json = json_aux::json_to_string(val);
			const unsigned L = (unsigned)sol_json.length();

			gbsr.buffer_size = L+1;
			gbsr.result = SolutionHubResults::SH_NO_ERROR;
		}
		else if(msg == NPPM_SOLUTIONHUB_CONFIG_GET_CONNECTIONS_BUFFERSIZE)
		{
			GetBufferSizeRequest &gbsr = *((GetBufferSizeRequest*)info);
			Json::Value val;
			if(!get_connections(val)) {
				gbsr.result = SolutionHubResults::SH_ERROR;
				return;
			}

			const std::string sol_json = json_aux::json_to_string(val);
			const unsigned L = (unsigned)sol_json.length();

			gbsr.buffer_size = L+1;
			gbsr.result = SolutionHubResults::SH_NO_ERROR;
		}

	} // on_config_message

	void on_hub_message(long msg, const wchar_t *plugin, void *info)
	{
		if(msg == NPPM_SOLUTIONHUB_QUERY_ATTRIBUTE || msg == NPPM_SOLUTIONHUB_QUERY_ATTRIBUTE_HOOKED)
		{
			AttributeQuery &aq = *((AttributeQuery*)info);

			std::string solution_name;
			if(msg == NPPM_SOLUTIONHUB_QUERY_ATTRIBUTE) {
				solution_name = aq.solution_name;
			} else {
				const std::string &alias = receiver_to_alias[plugin];
				if(!alias.empty())
					solution_name = get_solutionname_by_alias(alias.c_str());
			}

			char *result_buffer = aq.result_buffer;
			if(solution_name.empty()) {
				*result_buffer = 0;
				aq.result = SolutionHubResults::SH_ERROR_NO_SOLUTION;
				return;
			}

			const char *attribute = aq.attribute_name;

			std::string attribute_value = get_named_attribute(solution_name, attribute);
			if(attribute_value.empty()) {
				*result_buffer = 0;
				aq.result = SolutionHubResults::SH_ERROR_NO_ATTRIBUTE;
				return;
			}

			//int result_buffer_len = aq.result_buffersize;

			unsigned len = (unsigned)attribute_value.length();
			if(aq.result_buffersize > len) {
				strcpy(result_buffer, attribute_value.c_str());
				aq.result = SolutionHubResults::SH_NO_ERROR;
			} else {
				aq.result = SolutionHubResults::SH_ERROR_BUFFER_TO_SMALL;
			}
		}
		else if(msg == NPPM_SOLUTIONHUB_GET_ATTRIBUTES_HOOKED || msg == NPPM_SOLUTIONHUB_GET_ATTRIBUTES)
		{
			GetAttributesQuery &aq = *((GetAttributesQuery*)info);

			std::string solution_name;
			if(msg == NPPM_SOLUTIONHUB_GET_ATTRIBUTES) {
				solution_name = aq.solution_name;
			} else {
				const std::string &alias = receiver_to_alias[plugin];
				if(!alias.empty())
					solution_name = get_solutionname_by_alias(alias.c_str());
			}

			char *result_buffer = aq.result_buffer;
			if(solution_name.empty()) {
				*result_buffer = 0;
				aq.result = SolutionHubResults::SH_ERROR_NO_SOLUTION;
				return;
			}

			Json::Value attributes;
			if(!get_attributes(solution_name, attributes)) {
				*result_buffer = 0;
				aq.result = SolutionHubResults::SH_ERROR_NO_ATTRIBUTE;
				return;
			}

			const std::string as = json_aux::json_to_string(attributes);
			unsigned L = (unsigned)as.length();

			//int result_buffer_len = aq.result_buffersize;

			if(L > aq.result_buffersize) {
				aq.result = SolutionHubResults::SH_ERROR_BUFFER_TO_SMALL;
				return;
			}

			strcpy(result_buffer, as.c_str());
			aq.result = SolutionHubResults::SH_NO_ERROR;
		}
		else if(msg == NPPM_SOLUTIONHUB_HOOK_RECEIVER)
		{
			SolutionHubRegisterContext &rc = *((SolutionHubRegisterContext*)info);
			int marker = *(int*)(info);
			if(marker != SHMarker::marker) {
				rc.result = SolutionHubResults::SH_ERROR_INCOMPATIBLE_VERSION;
				return;
			}

			const char *alias = rc.alias;
			unsigned int mask = rc.mask;
			bool indexed = mask & NPP_SH_RCMASK_INDEXING;
			if(indexed) {
				indexed_aliases.insert(std::make_pair(alias, std::make_pair(true, std::string())));
				check_solution_indexing();
			}

			std::string current = receiver_to_alias[plugin];

			if(current.empty()) {
				receiver_to_alias[plugin] = alias;
				rc.result = SolutionHubResults::SH_NO_ERROR;
			} else {
				rc.result = SolutionHubResults::SH_ERROR_HOOK_DEFINED;
			}

		}
		else if(msg == NPPM_SOLUTIONHUB_UNHOOK_RECEIVER)
		{
			receiver_to_alias[plugin] = "";
		}
		else if(msg == NPPM_SOLUTIONHUB_SEARCH_SOLUTION)
		{
			SearchRequest &sr = *((SearchRequest*)(info));
			std::string alias = receiver_to_alias[plugin];

			if(alias.empty()) {
				sr.result = SolutionHubResults::SH_ERROR_NO_CONNECTION;
				return;
			}

			std::string solution_name = get_solutionname_by_alias(alias.c_str());
			FileRepositoryHandle handle = solution_to_repo_map[solution_name];
			if(!handle) {
				sr.result = SolutionHubResults::SH_ERROR_NO_CONNECTION;
				return;
			}

			SearchWrapper *sw = searchwrapper_make(plugin, sr.result_notification, sr.userdata, sr.userdata_size);

			sr.result = SolutionHubResults::SH_NO_ERROR; // just in case the search will respond BEFORE check...
			filerepo::search(handle, sr.searchstring, (void*)sw, filerepo_search_callback);
		}
	}
}

namespace npp_plugin_solutionhub {

	void init()
	{
		init_settingsfile();

		alias_to_solutionname.clear();
		setup_alias_mappings();
	}

	void terminate()
	{
		std::map<std::string, FileRepositoryHandle>::iterator i(solution_to_repo_map.begin()), end(solution_to_repo_map.end());
		while(i!=end) {
			if(i->second)
				filerepo::stop(i->second);

			++i;
		}
	}

	void on_message(const PluginMessage &pm)
	{
		long msg = pm.msg;
		const wchar_t *plugin = pm.src_module;
		void *info = pm.info;

		if(msg >= NPPM_SOLUTIONHUB_CONFIG_START)
			on_config_message(msg, plugin, info);
		else
			on_hub_message(msg, plugin, info);

#if 0
		static bool test_attribute_q = false;
		bool is_q = msg == NPPM_SOLUTIONHUB_QUERY_ATTRIBUTE || msg == NPPM_SOLUTIONHUB_QUERY_ATTRIBUTE_HOOKED;
		if(test_attribute_q && !is_q)
		{
			receiver_to_alias[npp_plugin::module_name()] = "svn"; // make temp alias...

			const char *attribute_name = "my_key";
			char buffer[256] = {};

			AttributeQuery aq = {};
			aq.solution_name = "test";
			aq.attribute_name = attribute_name;
			aq.result_buffer = buffer;
			aq.result_buffersize = 256;

			PluginMessage m = {};
			m.msg = NPPM_SOLUTIONHUB_QUERY_ATTRIBUTE;
			m.src_module = npp_plugin::module_name();
			m.info = &aq;
			on_message(m);

			aq.solution_name = 0;
			aq.attribute_name = "my_key2";
			m.msg = NPPM_SOLUTIONHUB_QUERY_ATTRIBUTE_HOOKED;

			on_message(m);
			int a = 1;
		}
#endif
	}
}
