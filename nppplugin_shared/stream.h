#pragma once

#include <vector>
#include <stdio.h>

namespace npp {
	namespace stream {
		inline void consume(std::vector<char> &v, unsigned n) {
			v.erase(v.begin(), v.begin()+n);
		}

		inline void make_room(std::vector<char> &v, unsigned num_bytes) {
			v.resize(v.size() + num_bytes);
		}

		template <class T> inline void pack(std::vector<char> &v, T t) {
			v.resize(v.size() + sizeof(T));
			memmove(&v[0] + v.size() - sizeof(T), &t, sizeof(T));
		}

		inline void pack_bytes(std::vector<char> &v, const void *p, unsigned count) {
			v.resize(v.size() + count);
			memmove(&v[0] + v.size() - count, p, count);
		}

		inline void pack_string(std::vector<char> &v, const char *s) {
			unsigned l = (unsigned)strlen(s);
			pack(v, l+1);
			pack_bytes(v, s, l);
			pack(v, (char)0);
		}

		inline void pack_string_wide(std::vector<char> &v, const wchar_t *s) {
			unsigned l = (unsigned)wcslen(s);
			pack(v, l+1);
			pack_bytes(v, s, l*sizeof(wchar_t));
			pack(v, (wchar_t)0);
		}

		inline void advance(const char *&stream, unsigned count) {
			stream += count;
		}

		template <class T> inline const T &unpack(const char *&stream) {
			const T* t = (const T *)stream;
			stream += sizeof(T);
			return *t;
		}
	}
}
