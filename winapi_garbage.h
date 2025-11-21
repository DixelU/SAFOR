#ifndef WINAPI_GARBAGE
#define WINAPI_GARBAGE


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
	inline HWND hList, hEdit;
	inline int removal_mode_line = -1;
	inline int velocity_threshold = -1;

	inline LRESULT CALLBACK ModeSelectorMessageHandler(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
	{
		switch (uMsg)
		{
			case WM_CLOSE:
			case WM_DESTROY:
				PostQuitMessage(0);
				break;
			case WM_COMMAND:
				if (LOWORD(wParam) == ID_SELECT_BUTTON)
				{
					auto selected = SendMessage(hList, LB_GETCURSEL, 0, 0);
					if (selected >= 0)
					{
						removal_mode_line = selected;
						PostQuitMessage(0);
						DestroyWindow(hWnd);
					}
				}
				break;
			default:
				break;
		}
		return DefWindowProc(hWnd, uMsg, wParam, lParam);
	}

	inline int GetMode()
	{
		INITCOMMONCONTROLSEX icex;
		
		icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
		icex.dwICC = ICC_TAB_CLASSES;
		InitCommonControlsEx(&icex);
	    
		HINSTANCE hInstance = GetModuleHandle(nullptr);
		HWND hWnd;
		HWND hButton;
		HFONT hFnt;
		WNDCLASS wc;
		MSG msg;
	
		wc = {};
		wc.style = CS_HREDRAW | CS_VREDRAW;
		wc.lpfnWndProc = ModeSelectorMessageHandler;
		wc.hInstance = hInstance;
		wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH));
		wc.lpszClassName = "ModeSelector";
		
		RegisterClass(&wc);
		
		NONCLIENTMETRICSA SystemMetrics;
		SystemMetrics.cbSize = sizeof(NONCLIENTMETRICS);
		SystemParametersInfo(SPI_GETNONCLIENTMETRICS, sizeof(NONCLIENTMETRICS), &SystemMetrics, 0);
		hFnt = CreateFontIndirect(&SystemMetrics.lfCaptionFont);
	
		hWnd = CreateWindow("ModeSelector", "Removal mode",
			WS_VISIBLE | WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, CW_USEDEFAULT, CW_USEDEFAULT, 178,
			200, nullptr, nullptr, hInstance, nullptr);

		hButton = CreateWindow("button", "Choose mode", WS_TABSTOP | WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 10,
			10, 150, 25, hWnd, (HMENU)ID_SELECT_BUTTON, hInstance, nullptr);

		hList = CreateWindowEx(WS_EX_CLIENTEDGE, "listbox", "",
			WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_AUTOVSCROLL, 10, 40, 150, 100, hWnd,
			reinterpret_cast<HMENU>(ID_LISTBOX), nullptr, nullptr);
		
		std::vector<std::string> modes = {"Overlaps", "Sustains and overlaps", "Velocity threshold"};
		for(const auto& single_mode: modes)
			SendMessage(hList, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(single_mode.c_str()));
		
		SendMessage(hList, LB_SETCURSEL, 0, 0);
			
		SendMessage(hButton, WM_SETFONT, reinterpret_cast<WPARAM>(hFnt), TRUE);
		SendMessage(hList, WM_SETFONT, reinterpret_cast<WPARAM>(hFnt), TRUE);
			
		ShowWindow(hList, SW_SHOW);
		UpdateWindow(hList);
		
		while ( GetMessage(&msg, nullptr, 0, 0))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	    
		return removal_mode_line;
	}

	inline LRESULT CALLBACK ThresholdMessageHandler(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
	{
		switch (uMsg)
		{
			case WM_CLOSE:
			case WM_DESTROY:
				PostQuitMessage(0);
				break;
			case WM_COMMAND:
				if (LOWORD(wParam) == ID_SELECT_BUTTON)
				{
					TCHAR text[5];
					text[0] = 4;
					auto count =
						SendMessage(hEdit, EM_GETLINE, 0,
							reinterpret_cast<LPARAM>(&text));

					std::string retrieved;
					for(int i = 0; i < count; ++i)
						retrieved.push_back(text[i]);

					if (!retrieved.empty())
					{
						try
						{
							if(int vol = std::stoi(retrieved); vol >= 0 && vol <= 127)
							{
								velocity_threshold = vol;
								PostQuitMessage(0);
								DestroyWindow(hWnd);
							}
						}
						catch (...) { }
					}
				}
				break;
			default:
				break;
		}
		return DefWindowProc(hWnd, uMsg, wParam, lParam);
	}

	inline int GetThreshold()
	{
		HINSTANCE hInstance = GetModuleHandle(nullptr);
		HWND hWnd;
		HWND hButton;
		HFONT hFnt;
		WNDCLASS wc;
		MSG msg;
	
		wc = {};
		wc.style = CS_HREDRAW | CS_VREDRAW;
		wc.lpfnWndProc = ThresholdMessageHandler;
		wc.hInstance = hInstance;
		wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH));
		wc.lpszClassName = "ThresholdSelector";
		
		RegisterClass(&wc);
		
		NONCLIENTMETRICSA SystemMetrics;
		SystemMetrics.cbSize = sizeof(NONCLIENTMETRICS);
		SystemParametersInfo(SPI_GETNONCLIENTMETRICS, sizeof(NONCLIENTMETRICS), &SystemMetrics, 0);
		hFnt = CreateFontIndirect(&SystemMetrics.lfCaptionFont);
	
		hWnd = CreateWindow("ThresholdSelector", "Threshold prompt",
			WS_VISIBLE | WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, CW_USEDEFAULT, CW_USEDEFAULT,
			178, 100, nullptr, nullptr, hInstance, nullptr);

		hButton = CreateWindow("button", "Enter", WS_TABSTOP | WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
			10, 10, 150, 25, hWnd, (HMENU)ID_SELECT_BUTTON, hInstance, nullptr);

		hEdit = CreateWindowEx(WS_EX_CLIENTEDGE, "edit", "",
			WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_LEFT | ES_NUMBER,
			10, 40, 150, 20, hWnd, reinterpret_cast<HMENU>(ID_EDITBOX),
			nullptr, nullptr);
		
		SendMessage(hEdit, EM_LIMITTEXT, 0, 4);
		SendMessage(hButton, WM_SETFONT, reinterpret_cast<WPARAM>(hFnt), TRUE);
		SendMessage(hEdit, WM_SETFONT, reinterpret_cast<WPARAM>(hFnt), TRUE);
		
		ShowWindow(hEdit, SW_SHOW);
		UpdateWindow(hEdit);
		
		while (GetMessage(&msg, nullptr, 0, 0))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		return velocity_threshold;
	}
}

#endif