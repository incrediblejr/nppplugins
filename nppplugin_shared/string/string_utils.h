#pragma once

#include <string>
#include <vector>

namespace string_util {
	std::wstring utf8_to_wstr(const char *utf8);

	std::wstring to_wide(const char *s);
	std::string from_wide(const wchar_t *s);

	bool isletter(int c);

	bool str_ends_with(const wchar_t *a, const wchar_t *end, bool case_sensitive);

	bool ends_with(const char *s, char c);
	bool ends_with(const wchar_t *s, char c);

	bool contains_tokens(const wchar_t *pattern, const wchar_t *tokens, bool case_sensitive, const wchar_t token_sep);

	const char *stristr(const char *haystack, const char *needle);
	const wchar_t *wstristr(const wchar_t *haystack, const wchar_t *needle);

	unsigned tokenize(const std::string &to_tok, const std::string &delimiters, std::vector<std::string> &out, unsigned offset = 0);
	unsigned tokenize(const std::wstring &to_tok, const std::wstring &delimiters, std::vector<std::wstring> &out, unsigned offset = 0);
}

namespace file_util {
	const wchar_t *fileextension(const wchar_t *s, bool keep_dot);

	const char *filename(const char *full);
	const wchar_t *filename(const wchar_t *full);

	bool path(const wchar_t *full, std::wstring &res);

	// how many characters the path 'occupy' in full
	unsigned pathlength(const wchar_t *full);

	void append_slash(std::wstring &d);
}
