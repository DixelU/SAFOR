
#include <windows.h>
#include <process.h>
#include <uxtheme.h>

// TODO: refactor properly 
// https://gist.github.com/Pilzschaf/d950a86042c37a9c8d1a8b9b5f082fff
#define ID_SELECT_BUTTON 150
#define ID_LISTBOX 151
#define ID_EDITBOX 152

namespace winapi_garbage
{
	HWND hList, hEdit;
	int RemovalModeLine = -1;
	int VelocityThreshold = -1;
	
	LRESULT CALLBACK ModeSelectorMessageHandler(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
		switch (uMsg) {
		case WM_CLOSE:
		case WM_DESTROY:
			PostQuitMessage(0);
			break;
		case WM_COMMAND:
			if (LOWORD(wParam) == ID_SELECT_BUTTON) {
				auto selected = SendMessage(hList, LB_GETCURSEL, 0, 0);
				if (selected >= 0) {
					RemovalModeLine = selected;
					PostQuitMessage(0);
					DestroyWindow(hWnd);
				}
			}
			break;
		}
		return DefWindowProc(hWnd, uMsg, wParam, lParam);
	}
	
	int GetMode() {  
		INITCOMMONCONTROLSEX icex;
		
	    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
	    icex.dwICC = ICC_TAB_CLASSES;
	    InitCommonControlsEx(&icex);
	    
		HINSTANCE hInstance = GetModuleHandle(0);
		HWND hWnd;
		HWND hButton;
		HFONT hFnt;
		WNDCLASS wc;
		MSG msg;
	
		wc = {};
		wc.style = CS_HREDRAW | CS_VREDRAW;
		wc.lpfnWndProc = ModeSelectorMessageHandler;
		wc.hInstance = hInstance;
		wc.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
		wc.lpszClassName = "ModeSelector";
		
		RegisterClass(&wc);
		
		NONCLIENTMETRICSA SystemMetrics;
		SystemMetrics.cbSize = sizeof(NONCLIENTMETRICS);
		SystemParametersInfo(SPI_GETNONCLIENTMETRICS, sizeof(NONCLIENTMETRICS), &SystemMetrics, 0);
		hFnt = CreateFontIndirect(&SystemMetrics.lfCaptionFont);
	
		hWnd = CreateWindow("ModeSelector", "Removal mode", WS_VISIBLE | WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, CW_USEDEFAULT, CW_USEDEFAULT, 178, 200, 0, 0, hInstance, 0);
		hButton = CreateWindow("button", "Choose mode", WS_TABSTOP | WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 10, 10, 150, 25, hWnd, (HMENU)ID_SELECT_BUTTON, hInstance, 0);
		hList = CreateWindowEx(WS_EX_CLIENTEDGE, "listbox", "", WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_AUTOVSCROLL, 10, 40, 150, 100, hWnd, (HMENU)ID_LISTBOX, 0, 0);
		
		std::vector<std::string> modes = {"Overlaps", "Sustains and overlaps", "Velocity threshold"};
		for(const auto& single_mode: modes)
			SendMessage(hList, LB_ADDSTRING, 0, (LPARAM)single_mode.c_str());
		
		SendMessage(hList, LB_SETCURSEL, 0, 0);
			
		SendMessage(hButton, WM_SETFONT, (WPARAM)hFnt, TRUE);
		SendMessage(hList, WM_SETFONT, (WPARAM)hFnt, TRUE);
			
		ShowWindow(hList, SW_SHOW);
	    UpdateWindow(hList);
		
		while (auto t = GetMessage(&msg, NULL, 0, 0)) {
	        TranslateMessage(&msg);
	        DispatchMessage(&msg);
	    }
	    
	    return RemovalModeLine;
	}
	
	LRESULT CALLBACK ThresholdMessageHandler(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
		switch (uMsg) {
		case WM_CLOSE:
		case WM_DESTROY:
			PostQuitMessage(0);
			break;
		case WM_COMMAND:
			if (LOWORD(wParam) == ID_SELECT_BUTTON) {
				TCHAR text[5];
				text[0] = 4;
				auto count = SendMessage(hEdit, EM_GETLINE, 0, (LPARAM)&text);
				std::string retrieved; 
				for(int i = 0; i < count; ++i)
					retrieved.push_back(text[i]);
				if (retrieved.size()) {
					try {
						int vol = std::stoi(retrieved);
						if(vol >= 0 && vol <= 127) {
							VelocityThreshold = vol;
							PostQuitMessage(0);
							DestroyWindow(hWnd);
						}
					} catch (...) { }
				}
			}
			break;
		}
		return DefWindowProc(hWnd, uMsg, wParam, lParam);
	}
	
	int GetTheshold() {
		HINSTANCE hInstance = GetModuleHandle(0);
		HWND hWnd;
		HWND hButton;
		HFONT hFnt;
		WNDCLASS wc;
		MSG msg;
	
		wc = {};
		wc.style = CS_HREDRAW | CS_VREDRAW;
		wc.lpfnWndProc = ThresholdMessageHandler;
		wc.hInstance = hInstance;
		wc.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
		wc.lpszClassName = "ThresholdSelector";
		
		RegisterClass(&wc);
		
		NONCLIENTMETRICSA SystemMetrics;
		SystemMetrics.cbSize = sizeof(NONCLIENTMETRICS);
		SystemParametersInfo(SPI_GETNONCLIENTMETRICS, sizeof(NONCLIENTMETRICS), &SystemMetrics, 0);
		hFnt = CreateFontIndirect(&SystemMetrics.lfCaptionFont);
	
		hWnd = CreateWindow("ThresholdSelector", "Threshold prompt", WS_VISIBLE | WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, CW_USEDEFAULT, CW_USEDEFAULT, 178, 100, 0, 0, hInstance, 0);
		hButton = CreateWindow("button", "Enter", WS_TABSTOP | WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 10, 10, 150, 25, hWnd, (HMENU)ID_SELECT_BUTTON, hInstance, 0);
		hEdit = CreateWindowEx(WS_EX_CLIENTEDGE, "edit", "", WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_LEFT | ES_NUMBER, 10, 40, 150, 20, hWnd, (HMENU)ID_EDITBOX, 0, 0);
		
		SendMessage(hEdit, EM_LIMITTEXT, 0, 4);
		SendMessage(hButton, WM_SETFONT, (WPARAM)hFnt, TRUE);
		SendMessage(hEdit, WM_SETFONT, (WPARAM)hFnt, TRUE);
		
		ShowWindow(hEdit, SW_SHOW);
	    UpdateWindow(hEdit);
		
		while (auto t = GetMessage(&msg, NULL, 0, 0)) {
	        TranslateMessage(&msg);
	        DispatchMessage(&msg);
	    }
	    return VelocityThreshold;
	}
}

