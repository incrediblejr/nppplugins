#pragma once

typedef int FolderMonitorHandle;

struct FolderMonitorDirectoryRecord {
	char *path;
	char *filter;

	bool is_file;
	bool recursive;
};

struct FolderMonitorFileRecord {
	char *path;
	char *file;
};

// Mirror of systemtime so we not have to include windows/winbase
struct ChangeTime {
	unsigned short Year;
	unsigned short Month;
	unsigned short DayOfWeek;
	unsigned short Day;
	unsigned short Hour;
	unsigned short Minute;
	unsigned short Second;
	unsigned short Milliseconds;
};

struct FolderMonitorChangeRecord {
	enum ActionType {
		CHANGE_ACTION_ADDED = 0,
		CHANGE_ACTION_REMOVED,
		CHANGE_ACTION_MODIFIED,
		CHANGE_ACTION_RENAMED_OLD_NAME,
		CHANGE_ACTION_RENAMED_NEW_NAME
	};
	int action;

	char *file;
	char *path;
	char *full_filepath;

	bool is_directory;
	ChangeTime time;
};

// userdata = that was provided while reg
// userdata, changerecords, num records
typedef void(*fm_notify_func)(void*, FolderMonitorChangeRecord**, int);

struct FolderMonitorRegisterContext {
	void *user_data;

	fm_notify_func notify_function;
};
