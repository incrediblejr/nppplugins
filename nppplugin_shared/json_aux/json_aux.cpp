#include "json_aux.h"

#define USE_FILE_SYSTEM 1

#if defined USE_FILE_SYSTEM
	#include "file/file_system.h"
#else
	#include <cstdlib>
	#include <fstream>
	#include <iostream>
	#include <sstream>
#endif

namespace json_aux {
	std::string json_to_string(const Json::Value &val) {
		Json::FastWriter writer;
		return writer.write(val);
	}

	bool json_from_string(const char *start, const char *end, Json::Value &out) {
		static bool collect_comments = false;
		Json::Reader parser;

		const bool res = parser.parse(start, end, out, collect_comments);
		if(!res) {
			std::string error = parser.getFormatedErrorMessages();
			int breakhere = 1;
			breakhere = 0;
		}

		return res;
	}

	bool json_from_string(const std::string &s, Json::Value &out) {
		const char *start = &s[0];
		const char *end = start + s.length();

		return json_from_string(start, end, out);
	}

	bool json_from_file(const std::wstring &file, Json::Value &out) {
		#if defined USE_FILE_SYSTEM
			const wchar_t *path = file.c_str();
			using namespace npp;
			unsigned fs = file_system::filesize(path);
			if(!fs)
				return false;

			std::vector<char> T(fs);
			fs = file_system::read(path, &T[0], fs);
			if(!fs)
				return false;
			const char *start = &T[0];
			const char *end = start+fs;

			return json_aux::json_from_string(start, end, out);

		#else
			std::ifstream ifs(file.c_str());

			std::stringstream oss;
			oss << ifs.rdbuf();

			if(!ifs && !ifs.eof())
				return false;

			std::string contents(oss.str());
			return json_aux::json_from_string(contents, out);
		#endif
	}

	void json_to_file(const std::wstring &file, const Json::Value &val) {
		#if defined USE_FILE_SYSTEM
			using namespace npp;
			const wchar_t *path = file.c_str();
			const std::string T = json_aux::json_to_string(val);
			file_system::write(path, &T[0], (unsigned)T.length());
		#else
			std::ofstream f(file.c_str());
			f << val;
		#endif
	}

	bool json_diff_key(const char *key, const Json::Value &current, const Json::Value &new_config)
	{
		bool current_has = current[key].isString();
		bool new_has = new_config[key].isString();

		//! new was added
		if(new_has && !current_has)
			return true;

		//! check for deletion
		if(current_has && !new_has)
			return true;

		bool both_has = (current_has && new_has);
		if(!both_has)
			return false;

		const char *a_val = current[key].asCString();
		const char *b_val = new_config[key].asCString();

		return (_stricmp(a_val, b_val) != 0);
	}

}
