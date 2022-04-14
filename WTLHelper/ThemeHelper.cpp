#include "pch.h"
#include <detours.h>
#include "ThemeHelper.h"
#include "Theme.h"
#include "CustomEdit.h"
#include "SizeGrip.h"
#include "CustomStatusBar.h"
#include "CustomButton.h"
#include "CustomDialog.h"
#include "CustomHeader.h"
#include "CustomRebar.h"
#include "CustomListView.h"
#include "OwnerDrawnMenu.h"
#include "CustomTabControl.h"
#include <unordered_map>

const Theme* CurrentTheme;
std::atomic<int> SuspendCount;

static decltype(::GetSysColor)* OrgGetSysColor = ::GetSysColor;
static decltype(::GetSysColorBrush)* OrgGetSysColorBrush = ::GetSysColorBrush;
static decltype(::GetSystemMetrics)* OrgGetSystemMetrics = ::GetSystemMetrics;
static decltype(::SetTextColor)* OrgSetTextColor = ::SetTextColor;
static decltype(::ReleaseDC)* OrgReleaseDC = ::ReleaseDC;

thread_local std::unordered_map<HDC, DCOperation> SuspendedDCOperations;

COLORREF WINAPI HookedSetTextColor(HDC hdc, COLORREF color) {
	if (auto it = SuspendedDCOperations.find(hdc); it != SuspendedDCOperations.end() && (it->second & DCOperation::SetTextColor) == DCOperation::SetTextColor) {
		return ::GetTextColor(hdc);
	}
	return OrgSetTextColor(hdc, color);
}

int WINAPI HookedReleaseDC(HWND hWnd, HDC hDC) {
	SuspendedDCOperations.erase(hDC);
	return OrgReleaseDC(hWnd, hDC);
}

int WINAPI HookedGetSystemMetrics(_In_ int index) {
	return OrgGetSystemMetrics(index);
}

HBRUSH WINAPI HookedGetSysColorBrush(int index) {
	if (CurrentTheme && SuspendCount == 0) {
		auto hBrush = CurrentTheme->GetSysBrush(index);
		if (hBrush)
			return hBrush;
	}
	return OrgGetSysColorBrush(index);
}

COLORREF WINAPI HookedGetSysColor(int index) {
	if (CurrentTheme && SuspendCount == 0) {
		auto color = CurrentTheme->GetSysColor(index);
		if (color != CLR_INVALID)
			return color;
	}
	return OrgGetSysColor(index);
}

void HandleCreateWindow(CWPRETSTRUCT* cs) {
	CString name;
	CWindow win(cs->hwnd);
	auto lpcs = (LPCREATESTRUCT)cs->lParam;
	if (!::GetClassName(cs->hwnd, name.GetBufferSetLength(32), 32))
		return;

	if (name.CompareNoCase(WC_COMBOBOX) != 0) {
		if ((lpcs->style & (WS_THICKFRAME | WS_CAPTION | WS_POPUP | WS_DLGFRAME)) == 0)
			::SetWindowTheme(cs->hwnd, L" ", L"");
	}
	//if (name.CompareNoCase(L"EDIT") == 0 || name.CompareNoCase(L"ATL:EDIT") == 0) {
	//	auto win = new CCustomEdit;
	//	ATLVERIFY(win->SubclassWindow(cs->hwnd));
	//}
	if (name.CompareNoCase(WC_LISTVIEW) == 0) {
		::SetWindowTheme(cs->hwnd, nullptr, nullptr);
		auto win = new CCustomListView;
		win->SubclassWindow(cs->hwnd);
	}
	else if (name.CompareNoCase(WC_TREEVIEW) == 0) {
		::SetWindowTheme(cs->hwnd, nullptr, nullptr);
	}
	else if (name.CompareNoCase(WC_TABCONTROL) == 0 || name.CompareNoCase(L"ATL:" WC_TABCONTROL) == 0) {
		auto win = new CCustomTabControlParent;
		ATLVERIFY(win->SubclassWindow(lpcs->hwndParent));
		win->Init(cs->hwnd);
	}
	else if (name.CompareNoCase(REBARCLASSNAME) == 0) {
		//::SetWindowTheme(cs->hwnd, nullptr, nullptr);
		auto win = new CCustomRebar;
		win->SubclassWindow(cs->hwnd);
	}
	else if (name.CompareNoCase(TOOLBARCLASSNAME) == 0) {
		::SetWindowTheme(cs->hwnd, nullptr, nullptr);
	}
	else if (name.CompareNoCase(WC_HEADER) == 0) {
		::SetWindowTheme(cs->hwnd, nullptr, nullptr);
		auto win = new CCustomHeaderParent;
		win->SubclassWindow(lpcs->hwndParent);
		win->Init(cs->hwnd);
	}
	else if (name.CompareNoCase(L"#32770") == 0) {		// dialog
		auto win = new CCustomDialog;
		ATLVERIFY(win->SubclassWindow(cs->hwnd));
	}
	else if (name.CompareNoCase(STATUSCLASSNAME) == 0) {
		::SetWindowTheme(cs->hwnd, nullptr, nullptr);
		auto win = new CCustomStatusBar;
		ATLVERIFY(win->SubclassWindow(cs->hwnd));
	}
	else if (name.CompareNoCase(L"ScrollBar") == 0) {
		if (lpcs->style & (SBS_SIZEBOX | SBS_SIZEGRIP)) {
			auto win = new CSizeGrip;
			ATLVERIFY(win->SubclassWindow(cs->hwnd));
		}
		else {
			//auto win = new CCustomScrollBar;
			//win->SubclassWindow(cs->hwnd);
		}
	}
	else if (name.CompareNoCase(L"BUTTON") == 0) {
		auto type = lpcs->style & BS_TYPEMASK;
		if (type == BS_PUSHBUTTON || type == BS_DEFPUSHBUTTON) {
			auto win = new CCustomButtonParent;
			ATLVERIFY(win->SubclassWindow(::GetParent(cs->hwnd)));
		}
	}
}

LRESULT CALLBACK CallWndProc(int action, WPARAM wp, LPARAM lp) {
	if (SuspendCount == 0 && action == HC_ACTION) {
		auto cs = reinterpret_cast<CWPRETSTRUCT*>(lp);

		switch (cs->message) {
			case WM_CREATE:
				HandleCreateWindow(cs);
				break;

		}
	}

	return ::CallNextHookEx(nullptr, action, wp, lp);
}


bool ThemeHelper::Init(HANDLE hThread) {
	auto hook = ::SetWindowsHookEx(WH_CALLWNDPROCRET, CallWndProc, nullptr, ::GetThreadId(hThread));
	if (!hook)
		return false;

	if (NOERROR != DetourTransactionBegin())
		return false;

	DetourUpdateThread(hThread);
	DetourAttach((PVOID*)&OrgGetSysColor, HookedGetSysColor);
	DetourAttach((PVOID*)&OrgGetSysColorBrush, HookedGetSysColorBrush);
	DetourAttach((PVOID*)&OrgGetSystemMetrics, HookedGetSystemMetrics);
	DetourAttach((PVOID*)&OrgSetTextColor, HookedSetTextColor);
	DetourAttach((PVOID*)&OrgReleaseDC, HookedReleaseDC);
	auto error = DetourTransactionCommit();
	ATLASSERT(error == NOERROR);
	return error == NOERROR;
}

int ThemeHelper::Suspend() {
	return ++SuspendCount;
}

int ThemeHelper::Resume() {
	return --SuspendCount;
}

bool ThemeHelper::SuspendDCOperation(DCOperation op, HDC hdc) {
	auto it = SuspendedDCOperations.find(hdc);
	if (it == SuspendedDCOperations.end())
		SuspendedDCOperations.insert({ hdc, op });
	else
		it->second |= op;

	return true;
}

const Theme* ThemeHelper::GetCurrentTheme() {
	return CurrentTheme;
}

bool ThemeHelper::IsDefault() {
	return GetCurrentTheme() == nullptr || GetCurrentTheme()->IsDefault();
}

void ThemeHelper::SetCurrentTheme(const Theme& theme, HWND hWnd) {
	CurrentTheme = &theme;
	if (hWnd) {
		SendMessageToDescendants(hWnd, ::RegisterWindowMessage(L"WTLHelperUpdateTheme"), 0, reinterpret_cast<LPARAM>(&theme));
		::RedrawWindow(hWnd, nullptr, nullptr, RDW_ALLCHILDREN | RDW_INTERNALPAINT | RDW_INVALIDATE | RDW_UPDATENOW);
	}
}

void ThemeHelper::UpdateMenuColors(COwnerDrawnMenuBase& menu, bool dark) {
	//
	// customize menu colors
	//
	auto theme = GetCurrentTheme();
	auto& mtheme = theme->Menu;
	menu.SetBackColor(mtheme.BackColor);
	menu.SetTextColor(mtheme.TextColor);
	menu.SetSelectionTextColor(mtheme.SelectionTextColor);
	menu.SetSelectionBackColor(mtheme.SelectionBackColor);
	menu.SetSeparatorColor(mtheme.SeparatorColor);
}

void ThemeHelper::SendMessageToDescendants(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
	for (auto hWndChild = ::GetTopWindow(hWnd); hWndChild; hWndChild = ::GetNextWindow(hWndChild, GW_HWNDNEXT)) {
		::SendMessage(hWndChild, message, wParam, lParam);
		if (::GetTopWindow(hWndChild)) {
			SendMessageToDescendants(hWndChild, message, wParam, lParam);
		}
	}
}