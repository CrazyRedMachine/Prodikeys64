/*
 * Prodikeys64, a systray application for Prodikeys MIDI Interface
 * Copyright 2020, CrazyRedMachine
 *
 * Based on StealthDialog
 * Copyright 2003, Abraxas23
 *
 */

#include "stdafx.h"
#include "resource.h"
#include "prodikeys-core.h"

#define TRAYICONID	1//				ID number for the Notify Icon
#define SWM_TRAYMSG	WM_APP//		the message ID sent to our window
#define SWM_DISABLE_MIDI WM_APP + 1 //disable midi
#define SWM_ENABLE_MIDI WM_APP + 2 //enable midi
#define SWM_EXIT	WM_APP + 3//	close the window
#define SWM_INIT	WM_APP + 4//	close the window

// Global Variables:
HINSTANCE		hInst;	// current instance
NOTIFYICONDATA	niData;	// notify icon data

// Forward declarations of functions included in this code module:
BOOL				InitInstance(HINSTANCE, int);
BOOL				OnInitDialog(HWND hWnd);
void				ShowContextMenu(HWND hWnd);
ULONGLONG			GetDllVersion(LPCTSTR lpszDllName);

INT_PTR CALLBACK	DlgProc(HWND, UINT, WPARAM, LPARAM);

struct pcmidi_snd* pm;

/* Attach to the USB device and init default values (midi mode OFF, fn state OFF..) */
BOOL prodikeys_init(){
    BOOL ret = FALSE;
    int res                      = 0;  /* return codes from libusb functions */
    libusb_device_handle* handle = 0;  /* handle for USB device */
    //connect to prodikeys
    if (!prodikeys_claim_interface(&handle)){
        int msgBoxId = MessageBoxW(NULL, L"Couldn't find prodikeys device. Make sure WinUSB driver is installed on interface 1 and that the keyboard is connected to the computer.", L"Error", MB_ICONERROR|MB_ABORTRETRYIGNORE|MB_SETFOREGROUND);
        while (msgBoxId == IDRETRY) {
            if (!prodikeys_claim_interface(&handle)) {
                msgBoxId = MessageBoxW(NULL,
                                       L"Couldn't find prodikeys device. Make sure WinUSB driver is installed on interface 1 and that the keyboard is connected to the computer.",
                                       L"Error", MB_ICONERROR | MB_ABORTRETRYIGNORE | MB_SETFOREGROUND);
            } else msgBoxId = IDIGNORE;
        }
        switch(msgBoxId){
            case IDIGNORE:
                break;
            case IDABORT:
            default:
                goto error;
        }
    }

    pm = static_cast<pcmidi_snd *>(malloc(sizeof(struct pcmidi_snd)));
    pm->handle = handle;
    pm_init_values(pm);
    ret = TRUE;

error:
    return ret;

}

HANDLE hProdikeysWDThread;
DWORD   dwProdikeysWDThreadId;
HANDLE hProdikeysThread;
DWORD   dwProdikeysThreadId;

/* This is the main USB HID reading loop, gathering messages from the Prodikeys endpoint and handling them */
DWORD WINAPI HandleProdikeys() {
    int res;
    int numBytes                 = 0;  /* Actual bytes transferred. */
    uint8_t buffer[31];                /* 31 byte transfer buffer */
    while (pm->handle != NULL){
        /* Read keys. */
        memset(buffer, 0, 31);
        res = libusb_interrupt_transfer(pm->handle, 0x82, buffer, 31, &numBytes, 3000);
        if (0 == res) {
            if (buffer[0] == 0x03)
                pcmidi_handle_note_report(pm,buffer,numBytes);
            else
                pcmidi_handle_report_extra(pm,buffer,numBytes);
        }
    }
    return 0;
}

/* This is a watchdog meant to manage unexpected unplugging of the keyboard */
DWORD WINAPI ProdikeysWatchdog() {
    while (true){
        if (pm->handle == NULL) continue;
        if (!prodikeys_send_hid_data(pm->handle, 0xC4)){
            //Keyboard was disconnected. Close the main thread and restore the state to initial application startup state
            int msgboxID = MessageBoxW(NULL, L"Prodikeys disconnected. Please plug it back then click \"Connect\" to reattach.", L"Error", MB_ICONERROR|MB_RETRYCANCEL|MB_SETFOREGROUND);
            CloseHandle(hProdikeysThread);
            pm->midi_mode = false;
            if (pm->port){
                virtualMIDIClosePort( pm->port );
            }
            pm->port = NULL;
            int res = libusb_release_interface(pm->handle, 1);
            if (0 != res)
                MessageBoxW(NULL, L"Error releasing interface. It is recommended to exit the program, plug the keyboard then restart Prodikeys64.", L"Error", MB_ICONERROR|MB_SETFOREGROUND);
            pm->handle = NULL;
            switch (msgboxID)
            {
                case IDCANCEL:
                    break;
                case IDRETRY:
                    //prodikeys_init will take care of respawning "Cancel Retry Ignore" dialogboxes
                    if (prodikeys_init()) {
                        hProdikeysThread = CreateThread(
                                NULL,                   // default security attributes
                                0,                      // use default stack size
                                reinterpret_cast<LPTHREAD_START_ROUTINE>(HandleProdikeys),       // thread function name
                                NULL,          // argument to thread function
                                0,                      // use default creation flags
                                &dwProdikeysThreadId);   // returns the thread identifier
                    }
                    break;
            }
        };
        Sleep(3000);
    }
}

int APIENTRY _tWinMain(HINSTANCE hInstance,
                     HINSTANCE hPrevInstance,
                     LPTSTR    lpCmdLine,
                     int       nCmdShow)
{
	MSG msg;

	// Perform application initialization:
	if (!InitInstance (hInstance, nCmdShow)) return FALSE;

	//setup prodikeys threads (at this point connection to the device has been handled in InitInstance function call)
    hProdikeysThread = CreateThread(
            NULL,                   // default security attributes
            0,                      // use default stack size
            reinterpret_cast<LPTHREAD_START_ROUTINE>(HandleProdikeys),       // thread function name
            NULL,          // argument to thread function
            0,                      // use default creation flags
            &dwProdikeysThreadId);   // returns the thread identifier
    hProdikeysWDThread = CreateThread(
            NULL,                   // default security attributes
            0,                      // use default stack size
            reinterpret_cast<LPTHREAD_START_ROUTINE>(ProdikeysWatchdog),       // thread function name
            NULL,          // argument to thread function
            0,                      // use default creation flags
            &dwProdikeysWDThreadId);   // returns the thread identifier

    // Main message loop:
	while (GetMessage(&msg, NULL, 0, 0))
	{
		if (!IsDialogMessage(msg.hwnd,&msg))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}
	return (int) msg.wParam;
}

//	Initialize the window and tray icon, attempt connection to the prodikeys device and initialize it
BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
	 // store instance handle and create dialog
	hInst = hInstance;
	HWND hWnd = CreateDialog( hInstance, MAKEINTRESOURCE(IDD_DLG_DIALOG),
		NULL, (DLGPROC)DlgProc );
	if (!hWnd) {
	    MessageBoxW(NULL, L"error", L"error", 0);
	    return FALSE;
	}

	// Fill the NOTIFYICONDATA structure and call Shell_NotifyIcon

	// zero the structure - note:	Some Windows funtions require this but
	//								I can't be bothered which ones do and
	//								which ones don't.
	ZeroMemory(&niData,sizeof(NOTIFYICONDATA));

	// get Shell32 version number and set the size of the structure
	//		note:	the MSDN documentation about this is a little
	//				dubious and I'm not at all sure if the method
	//				bellow is correct
	ULONGLONG ullVersion = GetDllVersion(_T("Shell32.dll"));
	if(ullVersion >= MAKEDLLVERULL(5, 0,0,0))
		niData.cbSize = sizeof(NOTIFYICONDATA);
	else niData.cbSize = NOTIFYICONDATA_V2_SIZE;

	// the ID number can be anything you choose
	niData.uID = TRAYICONID;

	// state which structure members are valid
	niData.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;

	// load the icon
	niData.hIcon = (HICON)LoadImage(hInstance,MAKEINTRESOURCE(IDI_PIANOICON),
		IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),GetSystemMetrics(SM_CYSMICON),
		LR_DEFAULTCOLOR);

	// the window to send messages to and the message to send
	//		note:	the message value should be in the
	//				range of WM_APP through 0xBFFF
	niData.hWnd = hWnd;
    niData.uCallbackMessage = SWM_TRAYMSG;

	// tooltip message
    lstrcpyn(niData.szTip, _T("Prodikeys Midi Interface Driver"), sizeof(niData.szTip)/sizeof(TCHAR));

	Shell_NotifyIcon(NIM_ADD,&niData);

	// free icon handle
	if(niData.hIcon && DestroyIcon(niData.hIcon))
		niData.hIcon = NULL;

	// Initialize Prodikeys (connect and init startup values)
    return prodikeys_init();
}

BOOL OnInitDialog(HWND hWnd)
{
	HMENU hMenu = GetSystemMenu(hWnd,FALSE);
	if (hMenu)
	{
		AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
	}
	HICON hIcon = (HICON)LoadImage(hInst,
		MAKEINTRESOURCE(IDI_PIANOICON),
		IMAGE_ICON, 0,0, LR_SHARED|LR_DEFAULTSIZE);
	SendMessage(hWnd,WM_SETICON,ICON_BIG,(LPARAM)hIcon);
	SendMessage(hWnd,WM_SETICON,ICON_SMALL,(LPARAM)hIcon);
	return TRUE;
}

// Name says it all
void ShowContextMenu(HWND hWnd)
{
	POINT pt;
	GetCursorPos(&pt);
	HMENU hMenu = CreatePopupMenu();
	if(hMenu)
	{
	    //"Connect" button available only if the libusb handle is invalid
        if (pm->handle != NULL){
            InsertMenu(hMenu, -1, MF_BYPOSITION|MF_DISABLED, NULL, _T("Connected"));
        } else {
            InsertMenu(hMenu, -1, MF_BYPOSITION, SWM_INIT, _T("Connect"));
        }

        InsertMenu(hMenu, -1, MF_BYPOSITION|MF_SEPARATOR, NULL, NULL);

        //Activate midi is grayed when the device isn't attached, and is checked only when midi keys are active. You can also use the piano key on the keyboard instead
        if (pm->midi_mode){
            InsertMenu(hMenu, -1, MF_BYPOSITION|MF_CHECKED, SWM_DISABLE_MIDI, _T("Activate midi"));
        } else if (pm->handle!= NULL){
            InsertMenu(hMenu, -1, MF_BYPOSITION|MF_UNCHECKED, SWM_ENABLE_MIDI, _T("Activate midi"));
        } else {
            InsertMenu(hMenu, -1, MF_BYPOSITION|MF_UNCHECKED|MF_DISABLED, SWM_ENABLE_MIDI, _T("Activate midi"));
        }
        InsertMenu(hMenu, -1, MF_BYPOSITION|MF_SEPARATOR, NULL, NULL);
        InsertMenu(hMenu, -1, MF_BYPOSITION|MF_STRING, IDM_ABOUT, _T("About..."));
        InsertMenu(hMenu, -1, MF_BYPOSITION, SWM_EXIT, _T("Exit"));

		// note:	must set window to the foreground or the
		//			menu won't disappear when it should
		SetForegroundWindow(hWnd);

		TrackPopupMenu(hMenu, TPM_BOTTOMALIGN,
			pt.x, pt.y, 0, hWnd, NULL );
		DestroyMenu(hMenu);
	}
}

// Get dll version number
ULONGLONG GetDllVersion(LPCTSTR lpszDllName)
{
    ULONGLONG ullVersion = 0;
	HINSTANCE hinstDll;
    hinstDll = LoadLibrary(lpszDllName);
    if(hinstDll)
    {
        DLLGETVERSIONPROC pDllGetVersion;
        pDllGetVersion = (DLLGETVERSIONPROC)GetProcAddress(hinstDll, "DllGetVersion");
        if(pDllGetVersion)
        {
            DLLVERSIONINFO dvi;
            HRESULT hr;
            ZeroMemory(&dvi, sizeof(dvi));
            dvi.cbSize = sizeof(dvi);
            hr = (*pDllGetVersion)(&dvi);
            if(SUCCEEDED(hr))
				ullVersion = MAKEDLLVERULL(dvi.dwMajorVersion, dvi.dwMinorVersion,0,0);
        }
        FreeLibrary(hinstDll);
    }
    return ullVersion;
}

// Message handler for the app
INT_PTR CALLBACK DlgProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	int wmId, wmEvent;

	switch (message) 
	{
	case SWM_TRAYMSG:
		switch(lParam)
		{
		case WM_LBUTTONDBLCLK:
			ShowWindow(hWnd, SW_RESTORE);
			break;
		case WM_RBUTTONDOWN:
		case WM_CONTEXTMENU:
			ShowContextMenu(hWnd);
		}
		break;
	case WM_SYSCOMMAND:
		if((wParam & 0xFFF0) == SC_MINIMIZE)
		{
			ShowWindow(hWnd, SW_HIDE);
			return 1;
		}
	case WM_COMMAND:
		wmId    = LOWORD(wParam);
		wmEvent = HIWORD(wParam); 

		switch (wmId)
		{
		case IDOK:
			ShowWindow(hWnd, SW_HIDE);
			break;
		case SWM_EXIT:
			DestroyWindow(hWnd);
			break;
            case IDM_ABOUT:
                ShowWindow(hWnd, SW_RESTORE);
                break;
            case SWM_ENABLE_MIDI:
                prodikeys_enable_midi(pm);
                break;
            case SWM_DISABLE_MIDI:
                prodikeys_disable_midi(pm);
                break;
		    case SWM_INIT:
		        /* prodikeys_init */
		        if (prodikeys_init()) {
                    hProdikeysThread = CreateThread(
                            NULL,                   // default security attributes
                            0,                      // use default stack size
                            reinterpret_cast<LPTHREAD_START_ROUTINE>(HandleProdikeys),       // thread function name
                            NULL,          // argument to thread function
                            0,                      // use default creation flags
                            &dwProdikeysThreadId);   // returns the thread identifier
                }
		        break;
		}
		return 1;
	case WM_INITDIALOG:
		return OnInitDialog(hWnd);
	case WM_CLOSE:
		DestroyWindow(hWnd);
		break;
	case WM_DESTROY:
		niData.uFlags = 0;
		Shell_NotifyIcon(NIM_DELETE,&niData);
		PostQuitMessage(0);
		break;
	}
	return 0;
}
