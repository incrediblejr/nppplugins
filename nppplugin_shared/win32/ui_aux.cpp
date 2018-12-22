#include "win32/ui_aux.h"

#include <shlobj.h>
#pragma comment (lib, "comctl32")
#include <assert.h>

#include "json/json.h"
#include "string/string_utils.h"

namespace ui_aux {
	LONG width(const RECT &r)
	{
		return r.right - r.left;
	}

	LONG height(const RECT &r)
	{
		return r.bottom - r.top;
	}

	LONG half_width(const RECT &r)
	{
		return (width(r) / 2);
	}

	LONG half_height(const RECT &r)
	{
		return (height(r) / 2);
	}

	void center_window(HWND hwnd) {
		RECT rc_parent;
		HWND hParent = ::GetParent(hwnd);
		::GetClientRect(hParent, &rc_parent);

		//If window coordinates are all zero(ie,window is minimised),then assign desktop as the parent window.
		if(!rc_parent.left && !rc_parent.right && !rc_parent.top && !rc_parent.bottom) {
			//hParent = ::GetDesktopWindow();
			::ShowWindow(hParent, SW_SHOWNORMAL);
			::GetClientRect(hParent,&rc_parent);
		}

		POINT center;
		LONG h_width = half_width(rc_parent);
		LONG h_height = half_height(rc_parent);
		center.x = rc_parent.left + h_width;
		center.y = rc_parent.top + h_height;
		::ClientToScreen(hParent, &center);

		RECT rc_win;
		::GetWindowRect(hwnd, &rc_win);
		h_width = half_width(rc_win);
		h_height = half_height(rc_win);
		int x = center.x - h_width;
		int y = center.y - h_height;

		::SetWindowPos(hwnd, HWND_TOP, x, y, (int)width(rc_win), (int)height(rc_win), SWP_SHOWWINDOW);
	}

	void center_window_stretch(HWND hwnd, double width_percentage, double height_percentage) {
		RECT rc_parent;
		HWND hParent = ::GetParent(hwnd);
		::GetClientRect(hParent, &rc_parent);

		//If window coordinates are all zero(ie,window is minimised),then assign desktop as the parent window.
		if(!rc_parent.left && !rc_parent.right && !rc_parent.top && !rc_parent.bottom) {
			//hParent = ::GetDesktopWindow();
			::ShowWindow(hParent, SW_SHOWNORMAL);
			::GetClientRect(hParent,&rc_parent);
		}

		POINT center;
		double parent_width = width(rc_parent);
		double parent_height = height(rc_parent);

		LONG h_width = half_width(rc_parent);
		LONG h_height = half_height(rc_parent);
		center.x = rc_parent.left + h_width;
		center.y = rc_parent.top + h_height;
		::ClientToScreen(hParent, &center);

		double desired_w = parent_width*width_percentage;
		double half_desired_w = desired_w / 2;

		double desired_h = parent_height*height_percentage;
		double half_desired_h = desired_h / 2;

		RECT rc_win;
		::GetWindowRect(hwnd, &rc_win);
		int x = (int)(center.x - half_desired_w);
		int y = (int)(center.y - half_desired_h);

		::SetWindowPos(hwnd, HWND_TOP, x, y, (int)desired_w, (int)desired_h, SWP_SHOWWINDOW);
	}

	void fit_to_parent(HWND h, bool repaint) {
		HWND parent = ::GetParent(h);
		RECT r;
		::GetClientRect(parent,&r);

		::MoveWindow(h, 0, 0, r.right, r.bottom, repaint);
	}


	void listview::column_fit(HWND h, ui_aux::listview::column *columns, const int num_columns) {
		int i;
		RECT rect;

		::GetWindowRect(h, &rect);
		//::GetClientRect(handle, &rect);

		const int width = (int) ui_aux::width(rect);

		// bool horizontal_visible = ui_aux::scrollbar::visible(h);
		int scroll_width		= GetSystemMetrics(SM_CXVSCROLL);
		int border_width		= GetSystemMetrics(SM_CXBORDER);
		int avail_width			= width - scroll_width - border_width - 4;	// 4 to be safe

		for(i=0; i<num_columns; ++i) {
			const ui_aux::listview::column &current = columns[i];

			if(current.type == ui_aux::listview::column::CT_FIXED)
				avail_width -= current.value;
		}


		for(i=0; i<num_columns; ++i) {
			const ui_aux::listview::column &current = columns[i];
			int column_width = -1;

			if (current.type == ui_aux::listview::column::CT_FIXED) {
				column_width = current.value;
			} else {
				const float percentage		= static_cast<float>(current.value) / 100.0f;
				column_width				= static_cast<int>(static_cast<float>(avail_width) * percentage);
			}

			::SendMessage(h, LVM_SETCOLUMNWIDTH, i, column_width);
		}
	}

	void listview::column_setup(HWND h, column *columns, const int num_columns)	{
		LVCOLUMN col;
		for(int i=0; i<num_columns; i++) {
			const ui_aux::listview::column &current = columns[i];

			memset(&col, 0, sizeof(LVCOLUMN));
			col.mask	= LVCF_WIDTH|LVCF_TEXT|LVCF_SUBITEM|LVCF_FMT;
			col.fmt		= LVCFMT_LEFT;
			col.pszText = (LPWSTR)current.name;
			col.cx		= 0;

			SendMessage(h, LVM_INSERTCOLUMN, i, LPARAM(&col));
		}

		listview::column_fit(h, columns, num_columns);
	}

	void listview::clear_selection(HWND h)
	{
		LRESULT i = -1;
		while(true) {
			i = ::SendMessage(h, LVM_GETNEXTITEM, i, LVNI_SELECTED);
			if (i == -1)
				break;

			ListView_SetItemState(h, i, 0, LVIS_SELECTED|LVIS_FOCUSED);
		}
	}

	void listview::scroll_to_top(HWND h)
	{
		//int current			= SendMessage(h, LVM_GETNEXTITEM, -1, LVNI_SELECTED|LVNI_FOCUSED);
		LRESULT count			= SendMessage(h, LVM_GETITEMCOUNT, 0, 0);

		LVITEM item;
		item.stateMask		= LVIS_SELECTED | LVIS_FOCUSED;
		item.state			= LVIS_SELECTED | LVIS_FOCUSED;

		if (count > 0)
		{
			clear_selection(h);
			::SendMessage(h, LVM_SETITEMSTATE, 0, (LPARAM) &item);
			::SendMessage(h, LVM_ENSUREVISIBLE, 0, 0);
		}
	}

	void listview::scroll_to_buttom(HWND h)
	{
		//int current			= (int)SendMessage(h, LVM_GETNEXTITEM, -1, LVNI_SELECTED|LVNI_FOCUSED);
		int count			= (int)SendMessage(h, LVM_GETITEMCOUNT, 0, 0);

		LVITEM item;
		item.stateMask		= LVIS_SELECTED | LVNI_FOCUSED ;
		item.state			= LVIS_SELECTED | LVNI_FOCUSED ;

		if (count > 0)
		{
			clear_selection(h);
			::SendMessage(h, LVM_SETITEMSTATE, count - 1, (LPARAM) &item);
			::SendMessage(h, LVM_ENSUREVISIBLE, count - 1, 0);
		}
	}

	bool is_fs(HWND h, int index, int t)
	{
		LRESULT i = -1;

		while(true)	{
			i = ::SendMessage(h, LVM_GETNEXTITEM, i, t);
			if(i == -1)
				return false;

			if(i == index)
				return true;
		}
	}

	bool listview::is_selected(HWND h, int index)
	{
		return is_fs(h, index, LVNI_SELECTED);
	}

	bool listview::is_focused(HWND h, int index)
	{
		return is_fs(h, index, LVNI_FOCUSED);
	}

	bool scrollbar::visible(HWND h) {
		SCROLLBARINFO sb;
		sb.cbSize = sizeof(SCROLLBARINFO);
		BOOL res = ::GetScrollBarInfo(h, OBJID_HSCROLL, &sb);
		if (!res)
			return false;

		int state = sb.rgstate[0];

		return (state != STATE_SYSTEM_INVISIBLE);
	}

	namespace folder {
		int CALLBACK BrowseCallbackProc(HWND hwnd, UINT uMsg, LPARAM, LPARAM pData) {
			if (uMsg == BFFM_INITIALIZED)
				::SendMessage(hwnd, BFFM_SETSELECTION, TRUE, pData);
			return 0;
		}

		bool open_browser(HWND owner, const std::wstring &title, std::wstring &selected) {
			bool result(false);

			LPMALLOC pShellMalloc = 0;
			if (::SHGetMalloc(&pShellMalloc) == NO_ERROR) {
				static std::wstring last_seen_dir(L"");
				BROWSEINFO bi;
				memset(&bi, 0, sizeof(bi));
				wchar_t display_name[MAX_PATH] = { 0 };

				bi.hwndOwner = owner;
				bi.pszDisplayName = display_name;
				bi.lpszTitle = title.c_str();
				bi.ulFlags = BIF_RETURNONLYFSDIRS;
				bi.lpfn = BrowseCallbackProc;
				bi.lParam = (LPARAM)last_seen_dir.c_str();

				PIDLIST_ABSOLUTE pidl = SHBrowseForFolder(&bi);

				if (pidl) {
					wchar_t path_name[MAX_PATH];
					BOOL res = SHGetPathFromIDList(pidl, path_name);

					if(res)	{
						result = true;
						selected.assign(path_name);
						last_seen_dir.assign(path_name);
					}

					pShellMalloc->Free(pidl);
				}

				pShellMalloc->Release();
			}
			return result;
		}
	}
} // ui_aux
