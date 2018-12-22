#pragma once

#define SOLUTIONHUB_DLL_FILE_NAME L"nppplugin_solutionhub.dll"

struct PluginMessage { unsigned int msg; const wchar_t *src_module; void *info; };

struct SolutionHubResults
{
	enum
	{
		SH_NO_ERROR = 0,
		SH_ERROR,
		SH_ERROR_INCOMPATIBLE_VERSION,

		SH_ERROR_NO_CONNECTION,
		SH_ERROR_NO_SOLUTION,
		SH_ERROR_NO_ATTRIBUTE,

		SH_ERROR_HOOK_DEFINED,		// A plugin has already used named alias
		SH_ERROR_BUFFER_TO_SMALL,
		SH_ERROR_INVALID_JSON
	};
};

#define NPPM_SOLUTIONHUB_START							100
#define NPP_SH_COM_INTERFACE_VERSION					2

#define NPP_SH_RCMASK_NONE								0x00000000	// No indexing needed
#define NPP_SH_RCMASK_INDEXING							0x00000001	// Indexing needed
#define NPP_SH_RCMASK_ATTACH							0x00000002	// Attach to another plugin alias

//! Hook a plugin with a named alias
struct SHMarker {
	static const int marker = NPP_SH_COM_INTERFACE_VERSION;
	SHMarker() : _marker(marker) {}
	const int _marker;
};

struct SolutionHubRegisterContext : SHMarker
{
	const char *alias;
	unsigned int mask;

	int result;
};
#define NPPM_SOLUTIONHUB_HOOK_RECEIVER					NPPM_SOLUTIONHUB_START+1
#define NPPM_SOLUTIONHUB_UNHOOK_RECEIVER				NPPM_SOLUTIONHUB_START+2

//! Queries
struct AttributeQuery
{
	const char *solution_name;	//! Solution name is ignored if using 'HOOKED' query
	const char *attribute_name;

	char *result_buffer;
	unsigned int result_buffersize;

	int result;
};

#define NPPM_SOLUTIONHUB_QUERY_ATTRIBUTE_HOOKED			NPPM_SOLUTIONHUB_START+3
#define NPPM_SOLUTIONHUB_QUERY_ATTRIBUTE				NPPM_SOLUTIONHUB_START+4

struct GetAttributesQuery
{
	const char *solution_name; //! Solution name is ignored if using 'HOOKED' query

	char *result_buffer;
	unsigned int result_buffersize;

	int result;
};
#define NPPM_SOLUTIONHUB_GET_ATTRIBUTES_HOOKED			NPPM_SOLUTIONHUB_START+5
#define NPPM_SOLUTIONHUB_GET_ATTRIBUTES					NPPM_SOLUTIONHUB_START+6

struct SearchRequest
{
	const wchar_t *searchstring;
	unsigned int result_notification;

	void *userdata;
	unsigned int userdata_size;

	int result;
};

struct SearchResponse
{
	const char *data;
	unsigned data_size;

	void *userdata;
	unsigned int userdata_size;
};

#define NPPM_SOLUTIONHUB_SEARCH_SOLUTION				NPPM_SOLUTIONHUB_START+7

//! Config/Settings messages START
#define NPPM_SOLUTIONHUB_CONFIG_START					500

//! Get solutions that is present in the SolutionHub
struct GetSolutionsRequest
{
	char *solution_json;
	unsigned int buffer_size;

	int result;
};
#define NPPM_SOLUTIONHUB_CONFIG_GET_SOLUTIONS			NPPM_SOLUTIONHUB_CONFIG_START

//! Get Json-formatted connection data
struct GetConnectionsRequest
{
	char *connection_json;
	unsigned int buffer_size;

	int result;
};
#define NPPM_SOLUTIONHUB_CONFIG_GET_CONNECTIONS			NPPM_SOLUTIONHUB_CONFIG_START+1

//! Save connections request
struct SaveConnectionsRequest
{
	const char *connection_json;

	int result;
};
#define NPPM_SOLUTIONHUB_CONFIG_SAVE_CONNECTIONS		NPPM_SOLUTIONHUB_CONFIG_START+2

//!
struct SaveSolutionRequest
{
	const char *solution_name;
	const char *solution_json;

	int result;
};
#define NPPM_SOLUTIONHUB_CONFIG_SAVE_SOLUTION			NPPM_SOLUTIONHUB_CONFIG_START+3

//!
struct DeleteSolutionRequest
{
	const char *solution_name;
	int result;
};
#define NPPM_SOLUTIONHUB_CONFIG_DELETE_SOLUTION			NPPM_SOLUTIONHUB_CONFIG_START+4

//! Connections will get white-space separated text with connected plugins (ex. 'ofis tsvn')
struct GetNamedConnectionsRequest
{
	char *connections;
	unsigned int buffer_size;

	int result;
};
#define NPPM_SOLUTIONHUB_CONFIG_GET_NAMED_CONNECTIONS	NPPM_SOLUTIONHUB_CONFIG_START+5

//! Add or overwrite existing attributes in a solution.
struct UpdateAttributesRequest
{
	const char *solution_name;	//! Solution name is ignored if using 'HOOKED' query
	const char *attribute_json;

	int result;
};
#define NPPM_SOLUTIONHUB_CONFIG_UPDATE_ATTRIBUTES			NPPM_SOLUTIONHUB_CONFIG_START+6
#define NPPM_SOLUTIONHUB_CONFIG_UPDATE_ATTRIBUTES_HOOKED	NPPM_SOLUTIONHUB_CONFIG_START+7

//! Get the size of minimum required buffersize for fetching solutions/connections.
struct GetBufferSizeRequest
{
	unsigned int buffer_size;
	int result;
};

#define NPPM_SOLUTIONHUB_CONFIG_GET_SOLUTIONS_BUFFERSIZE	NPPM_SOLUTIONHUB_CONFIG_START+8
#define NPPM_SOLUTIONHUB_CONFIG_GET_CONNECTIONS_BUFFERSIZE	NPPM_SOLUTIONHUB_CONFIG_START+9

//! Notifications start
#define NPPM_SOLUTIONHUB_NOTIFICATIONS_START			600

//! General connection info
struct ConnectionChangeInfo
{
	const char *new_solution;
	const char *prev_solution;
};
//! prev_solution field is valid
#define NPPN_SOLUTIONHUB_CONNECTION_DELETED				NPPM_SOLUTIONHUB_NOTIFICATIONS_START

//! prev_solution AND new_solution fields is valid
#define NPPN_SOLUTIONHUB_CONNECTION_CHANGED				NPPM_SOLUTIONHUB_NOTIFICATIONS_START+1

//! new_solution field is valid
#define NPPN_SOLUTIONHUB_CONNECTION_ADDED				NPPM_SOLUTIONHUB_NOTIFICATIONS_START+2

//!
struct SolutionChangeInfo
{
	const char *solution_name;
	const char *solution_json;
};
#define NPPN_SOLUTIONHUB_SOLUTION_UPDATED				NPPM_SOLUTIONHUB_NOTIFICATIONS_START+3
#define NPPN_SOLUTIONHUB_SOLUTION_DELETED				NPPM_SOLUTIONHUB_NOTIFICATIONS_START+4
