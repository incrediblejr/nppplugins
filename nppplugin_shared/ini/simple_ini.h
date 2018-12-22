#pragma once

typedef void (*simpleinihandler)(void *userdata, const char *key, const char *value);

namespace simple_ini
{
	//! Buffer will be modified
	void parse(char *buffer, simpleinihandler h, void *userdata);
}
