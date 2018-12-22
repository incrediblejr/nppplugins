#pragma once

#include "json/json.h"
#include <vector>

namespace json_aux {
	std::string json_to_string(const Json::Value &val);

	bool json_from_string(const char *start, const char *end, Json::Value &out);
	bool json_from_string(const std::string &s, Json::Value &out);

	bool json_from_file(const std::wstring &file, Json::Value &out);
	void json_to_file(const std::wstring &file, const Json::Value &val);

	// a is current, b is new
	bool json_diff_key(const char *key, const Json::Value &current, const Json::Value &new_config);
}
