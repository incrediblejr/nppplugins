#pragma once

#include <Windows.h>
#include <WindowsX.h>

#ifdef _WIN32_WINNT
#undef _WIN32_WINNT
	#define _WIN32_WINNT 0x0501
#endif

#include <CommCtrl.h>
#include <string>

namespace Json {
	class Value;
}

namespace ui_aux {
	template <typename T>
	struct key_value_pair_t { T key; T value; };
	typedef key_value_pair_t<wchar_t*> kv_pair_wstr;

	LONG width(const RECT &r);
	LONG height(const RECT &r);

	LONG half_width(const RECT &r);
	LONG half_height(const RECT &r);

	void center_window(HWND h);
	void center_window_stretch(HWND hwnd, double width_percentage, double height_percentage);

	void fit_to_parent(HWND h, bool repaint);

	namespace scrollbar	{
		bool visible(HWND h);
	}

	namespace listview {
		struct column {
			enum column_type {
				CT_PROPERTIONAL = 0,
				CT_FIXED
			};

			int type;
			int value;

			const wchar_t *name;
		};

		void column_setup(HWND h, ui_aux::listview::column *columns, const int num_columns);
		void column_fit(HWND h, ui_aux::listview::column *columns, const int num_columns);

		bool is_selected(HWND h, int index);
		bool is_focused(HWND h, int index);

		void clear_selection(HWND h);
		void scroll_to_top(HWND h);
		void scroll_to_buttom(HWND h);
	}

	namespace folder {
		bool open_browser(HWND owner, const std::wstring &title, std::wstring &selected);
	}
}
