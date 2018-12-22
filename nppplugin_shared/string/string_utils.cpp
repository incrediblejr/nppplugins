#include "string_utils.h"
#include <stdio.h>
#include <assert.h>

#include <Windows.h> // MultiByteToWideChar

#define strlen32(s) (unsigned)strlen((s))
#define wstrlen32(s) (unsigned)wcslen((s))

namespace string_util {
	std::wstring to_wide(const char *s) {
		unsigned L = MultiByteToWideChar(CP_ACP, 0, s, -1, 0, 0);

		std::vector<wchar_t> t(L);
		wchar_t *buf = &t[0];
		MultiByteToWideChar(CP_ACP, 0, s, L, buf, L);

		return std::wstring(buf);
	}

	std::string from_wide(const wchar_t *s) {
		unsigned L = WideCharToMultiByte(CP_ACP, 0, s, -1, 0, 0, 0, 0);

		std::vector<char> t(L);
		char *buf = &t[0];

		WideCharToMultiByte(CP_ACP, 0, s, L, buf, L, 0, 0);

		return std::string(buf);
	}

	std::wstring utf8_to_wstr(const char *utf8) {
		std::wstring res;
		if(!utf8)
			return res;

		unsigned wstring_length = (unsigned)MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8, -1, NULL, 0);

		if (!wstring_length)
			return res; // check error

		res.resize(wstring_length);

		wchar_t *wstring = &res[0];

		int written = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, wstring, wstring_length);
		if (written == 0)
			return res; // check error

		return res;
	}

	bool str_ends_with(const wchar_t *a, const wchar_t *end, bool case_sensitive)
	{
		unsigned sla = wstrlen32(a);
		unsigned slb = wstrlen32(end);
		if(slb > sla)
			return false;

		const wchar_t *starta = a+(sla-slb);

		return (0 == (case_sensitive ? wcscmp(starta, end) : _wcsicmp(starta, end)));
	}

	bool ends_with(const char *s, char c) {
		if(!s)
			return false;

		unsigned l = strlen32(s);
		if (!l)
			return false;

		return (s[l-1] == c);
	}

	bool ends_with(const wchar_t *s, char c) {
		if(!s)
			return false;

		unsigned l = wstrlen32(s);
		if (!l)
			return false;

		return (s[l-1] == c);
	}

	bool contains_tokens(const wchar_t *pattern, const wchar_t *tokens, bool case_sensitive, const wchar_t token_sep) {
		// special cases first
		if(!tokens)
			return false;
		if(!pattern)
			return true;	// consider no pattern a match

		typedef int (*comp)(const wchar_t*, const wchar_t*);

		comp comparer = (case_sensitive ? wcscmp : _wcsicmp);

		const unsigned pattern_length = wstrlen32(pattern);
		const unsigned str_length = wstrlen32(tokens);

		if(!pattern_length)
			return true;			// consider no pattern a match

		if (pattern_length > str_length)
			return false;

		const wchar_t *search_str = (case_sensitive ? wcsstr(tokens, pattern) : wstristr(tokens, pattern));
		while(search_str) {
			if (wstrlen32(search_str) != pattern_length) {
				if(token_sep == search_str[pattern_length]) {
					return true;
				}
				search_str = (case_sensitive ? wcsstr(&search_str[pattern_length], pattern) : wstristr(&search_str[pattern_length], pattern));
			} else {
				const bool at_start = search_str == tokens;
				if(at_start) {
					return comparer(search_str, pattern) == 0;
				} else {
					if (token_sep == *(search_str-1))
						return comparer(search_str, pattern) == 0;
					else
						return false;
				}

			}
		}

		return false;
	}

	bool isletter(int c) {
		if ((c >= 65 && c <= 90) || (c >= 97 && c <= 122))
			return true;

		return false;
	}

	// http://www.daniweb.com/code/snippet216564.html
	const char *stristr(const char *haystack, const char *needle) {
		if (!*needle)
			return haystack;
		for (; *haystack; ++haystack) {
			if (toupper(*haystack) == toupper(*needle)) {
				/*
				* Matched starting char -- loop through remaining chars.
				*/
				const char *h, *n;
				for (h=haystack, n=needle; *h && *n; ++h, ++n) {
					if (toupper(*h) != toupper(*n))
						break;
				}

				if (!*n) /* matched all of 'needle' to null termination */
					return haystack; /* return the start of the match */
			}
		}
		return 0;
	}

	const wchar_t *wstristr(const wchar_t *haystack, const wchar_t *needle) {
		if (!*needle)
			return haystack;
		for (; *haystack; ++haystack) {
			if (toupper(*haystack) == toupper(*needle)) {
				const wchar_t *h, *n;
				for (h=haystack, n=needle; *h && *n; ++h, ++n) {
					if (toupper(*h) != toupper(*n))
						break;
				}

				if (!*n)
					return haystack;
			}
		}
		return 0;
	}

	unsigned tokenize(const std::string &to_tok, const std::string &delimiters, std::vector<std::string> &out, unsigned offset /*= 0*/)	{
		size_t i = to_tok.find_first_not_of(delimiters, offset);

		if (std::string::npos == i)
			return (unsigned)out.size();

		size_t j = to_tok.find_first_of(delimiters, i);

		if (std::string::npos == j) {
			out.push_back(to_tok.substr(i));
			return (unsigned)out.size();
		}

		out.push_back(to_tok.substr(i, j-i));

		return tokenize(to_tok, delimiters, out, (unsigned)j);
	}

	unsigned tokenize(const std::wstring &to_tok, const std::wstring &delimiters, std::vector<std::wstring> &out, unsigned offset /*= 0*/)	{
		size_t i = to_tok.find_first_not_of(delimiters, offset);

		if (std::string::npos == i)
			return (unsigned)out.size();

		size_t j = to_tok.find_first_of(delimiters, i);

		if (std::wstring::npos == j) {
			out.push_back(to_tok.substr(i));
			return (unsigned)out.size();
		}

		out.push_back(to_tok.substr(i, j-i));

		return tokenize(to_tok, delimiters, out, (unsigned)j);
	}
}

namespace file_util {
	const char folder_separator = '\\';
	const char folder_separator_forward = '/';

	#define MAX(a, b)  (((a) > (b)) ? (a) : (b))
	#define MIN(a, b)  (((a) < (b)) ? (a) : (b))

	const char *filename(const char *full) {
		const char *last_sep = strrchr(full, folder_separator);

		if (!last_sep)
			return full;

		unsigned last_sep_len = strlen32(last_sep);
		if (1 == last_sep_len)
			return 0;		// ended with a slash ?
		else
			return last_sep+1;
	}

	unsigned pathlength(const wchar_t *full) {
		const wchar_t *last_sep = wcsrchr(full, folder_separator);
		if(!last_sep)
			return 0;

		unsigned len = wstrlen32(full);
		unsigned file_name_len = wstrlen32(last_sep);
		unsigned count = MAX( MIN(len-file_name_len+1, len), 0 );

		return count;
	}

	bool path(const wchar_t *full, std::wstring &res) {
		const wchar_t *last_sep = wcsrchr(full, folder_separator);
		if(!last_sep)
			return false;

		unsigned len = wstrlen32(full);
		unsigned file_name_len = wstrlen32(last_sep);
		unsigned count = MAX( MIN(len-file_name_len+1, len), 0 );
		if(count == 0)
			return false;

		res.assign(full, count);

		return true;
	}

	const wchar_t *filename(const wchar_t *full) {
		const wchar_t *last_sep = wcsrchr(full, folder_separator);

		if (!last_sep)
			return full;

		unsigned last_sep_len = wstrlen32(last_sep);
		if (last_sep_len == 1)
			return 0;		// ended with a slash ?

		return last_sep+1;
	}

	static bool ends_with_slash(const wchar_t *p) {
		if (!p) return false;

		unsigned len = wstrlen32(p);
		if (!len)
			return false;

		int last = p[len-1];
		return last == folder_separator || last == folder_separator_forward;
	}

	void append_slash(std::wstring &d) {
		if(!ends_with_slash(d.c_str()))
			d += L"\\";
	}

	const wchar_t *fileextension(const wchar_t *s, bool keep_dot) {
		const wchar_t *e = wcsrchr(s, L'.');
		if(e) {
			if(keep_dot)
				return e;

			unsigned l = wstrlen32(e);
			if(l > 1)
				return e+1;
		}
		return 0;
	}

}
