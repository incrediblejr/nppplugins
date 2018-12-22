#include "simple_ini.h"

#include <stdio.h>
#include <string.h>
#include <ctype.h>

namespace
{
	char *skipto(char *s, char c)
	{
		while(*s && *s != c)
			++s;
		return s;
	}

	char* rstrip_space(char* s)
	{
		char* p = s + strlen(s);
		while (p > s && isspace(*--p))
			*p = 0;
		return s;
	}

	char* lskip_space(char* s)
	{
		while (*s && isspace(*s))
			s++;
		return s;
	}
}

namespace simple_ini
{
	//! Buffer will be modified
	void parse(char *buffer, simpleinihandler h, void *userdata)
	{
		const unsigned BUFFER_SIZE = 256;
		char TEMP_BUFFER[BUFFER_SIZE];

		char *start, *end;
		char *s = buffer;

		while( *s && (s=lskip_space(s)) != 0 )
		{
			end = 0;
			start = s;

			while(*s && !end) {
				if(*s == '\r' || *s == '\n')
				{
					if (s[0]+s[1] == '\r' + '\n')
						++s;

					end = s-1;
				}
				++s;
			}

			end = (end ? end : s);

			if(start && end)
			{
				unsigned L = (unsigned)(end - start);
				if(L && (BUFFER_SIZE-1) > L) {

					strncpy(TEMP_BUFFER, start, L);
					TEMP_BUFFER[L] = 0;

					//! Already LEFT trimmed
					start = TEMP_BUFFER;

					if(*start == '#') {
						//! Comment skip
					} else {
						end = skipto(start,  ':');
						if(*end) {
							*end = 0;

							char *key = rstrip_space(start);
							char *value = rstrip_space( lskip_space(end+1) );

							h(userdata, key, value);
						}
					}
				}
			}


		}
	}
}
