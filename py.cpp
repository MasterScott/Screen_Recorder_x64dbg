#include "py.h"
#include "stringutils.h"
#include "resource.h"
#include <windows.h>
#include <stdio.h>
#include <psapi.h>
#include <python.h>
#include <shlwapi.h>

#pragma comment(lib, "shlwapi.lib")

#define module_name "X64Dbg_screen_record" //plugin name
#define event_object_name "Event"

// lParam: ScanCode=0x41(ALT), cRepeat=1, fExtended=False, fAltDown=True, fRepeat=False, fUp=False
// #define ALT_F7_SYSKEYDOWN 0x20410001 //hotkey if needed

PyObject* pModule, *pEventObject;
HINSTANCE hInst;

static void Recorder(); //edit this

extern "C" __declspec(dllexport) void CBMENUENTRY(CBTYPE cbType, PLUG_CB_MENUENTRY* info)
{
    switch(info->hEntry)
    {
	case MENU_RECORD: // edit this
		Recorder();
		break;
    }
}





static bool ExecutePythonScript(wchar_t* szFileName)
{
    int status_code;
    String szFileNameA = Utf16ToUtf8(szFileName);
    PyObject* PyFileObject = PyFile_FromString((char*)szFileNameA.c_str(), "r");
    if(PyFileObject == NULL)
    {
        _plugin_logprintf("[PYTHON] Could not open file....");
        PyErr_PrintEx(0);
        return false;
    }

    status_code = PyRun_SimpleFile(PyFile_AsFile(PyFileObject), (char*)szFileNameA.c_str());
    Py_DECREF(PyFileObject);
    if(status_code != EXIT_SUCCESS)
    {
        _plugin_logprintf("[PYTHON] Execution failed (status code: %d)....\n", status_code);
        PyErr_PrintEx(0);
        return false;
    }

    _plugin_logputs("[PYTHON] Execution is done!");
    return true;
}



// X64Dbg_screen_record
static void Recorder() // function to call main python app file
{
    wchar_t szFileName[MAX_PATH] = {0};
	char szDir[256]; GetCurrentDirectoryA(256,szDir); _plugin_logputs(szDir);
    ExecutePythonScript(L"plugins\\recorder\\rec_main.py"); // execution of main file location
}








static void cbUnloadDllCallback(CBTYPE cbType, void* info)
{
    PyObject* pFunc;
    PyObject* pKwargs, *pValue;

    LPUNLOAD_DLL_DEBUG_INFO UnloadDll = ((PLUG_CB_UNLOADDLL*)info)->UnloadDll;

    // Check if event object exist.
    if(pEventObject == NULL)
        return;

    pFunc = PyObject_GetAttrString(pEventObject, "unload_dll");
    if(pFunc && PyCallable_Check(pFunc))
    {
        pKwargs = Py_BuildValue(
                      "{s:N}",
                      "lpBaseOfDll", PyInt_FromSize_t((size_t)UnloadDll->lpBaseOfDll)
                  );
        pValue = PyObject_Call(pFunc, PyTuple_New(0), pKwargs);
        Py_DECREF(pKwargs);
        Py_DECREF(pFunc);
        if(pValue == NULL)
        {
            _plugin_logputs("[PYTHON] Could not use unload_dll function.");
            PyErr_PrintEx(0);
            return;
        }

        Py_DECREF(pValue);
    }
}

static void cbLoadDllCallback(CBTYPE cbType, void* info)
{
    PyObject* pFunc;
    PyObject* pLoadDll, *pPdbSig70, *pModInfo, *pKwargs, *pValue;

    PLUG_CB_LOADDLL* callbackInfo = (PLUG_CB_LOADDLL*)info;
    LOAD_DLL_DEBUG_INFO* LoadDll = callbackInfo->LoadDll;
    IMAGEHLP_MODULE64* modInfo = callbackInfo->modInfo;
    GUID PdbSig70 = modInfo->PdbSig70;

    // Check if event object exist.
    if(pEventObject == NULL)
        return;

    pFunc = PyObject_GetAttrString(pEventObject, "load_dll");
    if(pFunc && PyCallable_Check(pFunc))
    {
        pLoadDll = Py_BuildValue(
                       "{s:N, s:N, s:k, s:k, s:N, s:H}",
                       "hFile", PyInt_FromSize_t((size_t)LoadDll->hFile),
                       "lpBaseOfDll", PyInt_FromSize_t((size_t)LoadDll->lpBaseOfDll),
                       "dwDebugInfoFileOffset", LoadDll->dwDebugInfoFileOffset,
                       "nDebugInfoSize", LoadDll->nDebugInfoSize,
                       "lpImageName", PyInt_FromSize_t((size_t)LoadDll->lpImageName),
                       "fUnicode", LoadDll->fUnicode
                   );
        pPdbSig70 = Py_BuildValue(
                        "{s:k, s:H, s:H, s:N}",
                        "Data1", PdbSig70.Data1,
                        "Data2", PdbSig70.Data2,
                        "Data3", PdbSig70.Data3,
                        "Data4", PyByteArray_FromStringAndSize(
                            (char*)PdbSig70.Data4, ARRAYSIZE(PdbSig70.Data4)
                        )
                    );
        pModInfo = Py_BuildValue(
                       "{s:k, s:K, s:k, s:k, s:k, s:k, s:i, s:s, s:s, s:s, s:s, "
                       " s:k, s:s, s:k, s:N, s:k, s:N, s:N, s:N, s:N, s:N, s:N, s:N}",
                       "SizeOfStruct", modInfo->SizeOfStruct,
                       "BaseOfImage", modInfo->BaseOfImage,
                       "ImageSize", modInfo->TimeDateStamp,
                       "TimeDateStamp", modInfo->TimeDateStamp,
                       "CheckSum", modInfo->CheckSum,
                       "NumSyms", modInfo->NumSyms,
                       "SymType", modInfo->SymType,
                       "ModuleName", modInfo->ModuleName,
                       "ImageName", modInfo->ImageName,
                       "LoadedImageName", modInfo->LoadedImageName,
                       "LoadedPdbName", modInfo->LoadedPdbName,
                       "CVSig", modInfo->CVSig,
                       "CVData", modInfo->CVData,
                       "PdbSig", modInfo->PdbSig,
                       "PdbSig70", pPdbSig70,
                       "PdbAge", modInfo->PdbAge,
                       "PdbUnmatched", PyBool_FromLong(modInfo->PdbUnmatched),
                       "DbgUnmatched", PyBool_FromLong(modInfo->DbgUnmatched),
                       "LineNumbers", PyBool_FromLong(modInfo->LineNumbers),
                       "GlobalSymbols", PyBool_FromLong(modInfo->GlobalSymbols),
                       "TypeInfo", PyBool_FromLong(modInfo->TypeInfo),
                       "SourceIndexed", PyBool_FromLong(modInfo->SourceIndexed),
                       "Publics", PyBool_FromLong(modInfo->Publics)
                   );
        pKwargs = Py_BuildValue(
                      "{s:N, s:N, s:s}",
                      "LoadDll", pLoadDll,
                      "modInfo", pModInfo,
                      "modname", callbackInfo->modname
                  );
        pValue = PyObject_Call(pFunc, PyTuple_New(0), pKwargs);
        Py_DECREF(pLoadDll);
        Py_DECREF(pPdbSig70);
        Py_DECREF(pModInfo);
        Py_DECREF(pKwargs);
        Py_DECREF(pFunc);
        if(pValue == NULL)
        {
            _plugin_logputs("[PYTHON] Could not use load_dll function.");
            PyErr_PrintEx(0);
            return;
        }

        Py_DECREF(pValue);
    }
}



static void cbExitThreadCallback(CBTYPE cbType, void* info)
{
    PyObject* pFunc;
    PyObject* pKwargs, *pValue;

    PLUG_CB_EXITTHREAD* callbackInfo = ((PLUG_CB_EXITTHREAD*)info);

    // Check if event object exist.
    if(pEventObject == NULL)
        return;

    pFunc = PyObject_GetAttrString(pEventObject, "exit_thread");
    if(pFunc && PyCallable_Check(pFunc))
    {
        pKwargs = Py_BuildValue(
                      "{s:k, s:k}",
                      "dwThreadId", callbackInfo->dwThreadId,
                      "dwExitCode", callbackInfo->ExitThread->dwExitCode
                  );
        pValue = PyObject_Call(pFunc, PyTuple_New(0), pKwargs);
        Py_DECREF(pKwargs);
        Py_DECREF(pFunc);
        if(pValue == NULL)
        {
            _plugin_logputs("[PYTHON] Could not use exit_thread function.");
            PyErr_PrintEx(0);
            return;
        }

        Py_DECREF(pValue);
    }
}

static void cbCreateThreadCallback(CBTYPE cbType, void* info)
{
    PyObject* pFunc;
    PyObject* pCreateThread, *pKwargs, *pValue;

    PLUG_CB_CREATETHREAD* callbackInfo = (PLUG_CB_CREATETHREAD*)info;
    CREATE_THREAD_DEBUG_INFO* CreateThread = callbackInfo->CreateThread;

    // Check if event object exist.
    if(pEventObject == NULL)
        return;

    pFunc = PyObject_GetAttrString(pEventObject, "create_thread");
    if(pFunc && PyCallable_Check(pFunc))
    {
        pCreateThread = Py_BuildValue(
                            "{s:k, s:N, s:N}",
                            "hThread", CreateThread->hThread,
                            "lpThreadLocalBase", PyInt_FromSize_t((size_t)CreateThread->lpThreadLocalBase),
                            "lpStartAddress", PyInt_FromSize_t((size_t)CreateThread->lpThreadLocalBase)
                        );
        pKwargs = Py_BuildValue(
                      "{s:k, s:N}",
                      "dwThreadId", callbackInfo->dwThreadId,
                      "CreateThread", pCreateThread
                  );
        pValue = PyObject_Call(pFunc, PyTuple_New(0), pKwargs);
        Py_DECREF(pCreateThread);
        Py_DECREF(pKwargs);
        Py_DECREF(pFunc);
        if(pValue == NULL)
        {
            _plugin_logputs("[PYTHON] Could not use exit_process function.");
            PyErr_PrintEx(0);
            return;
        }

        Py_DECREF(pValue);
    }
}

static void cbExitProcessCallback(CBTYPE cbType, void* info)
{
    PyObject* pFunc;
    PyObject* pKwargs, *pValue;

    EXIT_PROCESS_DEBUG_INFO* ExitProcess = ((PLUG_CB_EXITPROCESS*)info)->ExitProcess;

    // Check if event object exist.
    if(pEventObject == NULL)
        return;

    pFunc = PyObject_GetAttrString(pEventObject, "exit_process");
    if(pFunc && PyCallable_Check(pFunc))
    {
        pKwargs = Py_BuildValue(
                      "{s:k}",
                      "dwExitCode", ExitProcess->dwExitCode
                  );
        pValue = PyObject_Call(pFunc, PyTuple_New(0), pKwargs);
        Py_DECREF(pKwargs);
        Py_DECREF(pFunc);
        if(pValue == NULL)
        {
            _plugin_logputs("[PYTHON] Could not use exit_process function.");
            PyErr_PrintEx(0);
            return;
        }

        Py_DECREF(pValue);
    }
}

static void cbCreateProcessCallback(CBTYPE cbType, void* info)
{
    PyObject* pFunc;
    PyObject* pCreateProcessInfo, *pPdbSig70, *pModInfo, *pFdProcessInfo, *pKwargs, *pValue;

    PLUG_CB_CREATEPROCESS* callbackInfo = (PLUG_CB_CREATEPROCESS*)info;
    CREATE_PROCESS_DEBUG_INFO* CreateProcessInfo = callbackInfo->CreateProcessInfo;
    IMAGEHLP_MODULE64* modInfo = callbackInfo->modInfo;
    PROCESS_INFORMATION* fdProcessInfo = callbackInfo->fdProcessInfo;
    GUID PdbSig70 = modInfo->PdbSig70;

    // Check if event object exist.
    if(pEventObject == NULL)
        return;

    pFunc = PyObject_GetAttrString(pEventObject, "create_process");
    if(pFunc && PyCallable_Check(pFunc))
    {
        pCreateProcessInfo = Py_BuildValue(
                                 "{s:N, s:N, s:N, s:N, s:k, s:k, s:N, s:N, s:N, s:H}",
                                 "hFile", PyInt_FromSize_t((size_t)CreateProcessInfo->hFile),
                                 "hProcess", PyInt_FromSize_t((size_t)CreateProcessInfo->hProcess),
                                 "hThread", PyInt_FromSize_t((size_t)CreateProcessInfo->hThread),
                                 "lpBaseOfImage", PyInt_FromSize_t((size_t)CreateProcessInfo->lpBaseOfImage),
                                 "dwDebugInfoFileOffset", CreateProcessInfo->dwDebugInfoFileOffset,
                                 "nDebugInfoSize", CreateProcessInfo->nDebugInfoSize,
                                 "lpThreadLocalBase", PyInt_FromSize_t((size_t)CreateProcessInfo->lpThreadLocalBase),
                                 "lpStartAddress", PyInt_FromSize_t((size_t)CreateProcessInfo->lpStartAddress),
                                 "lpImageName", PyInt_FromSize_t((size_t)CreateProcessInfo->lpImageName),
                                 "fUnicode", CreateProcessInfo->fUnicode
                             );
        pPdbSig70 = Py_BuildValue(
                        "{s:k, s:H, s:H, s:N}",
                        "Data1", PdbSig70.Data1,
                        "Data2", PdbSig70.Data2,
                        "Data3", PdbSig70.Data3,
                        "Data4", PyByteArray_FromStringAndSize(
                            (char*)PdbSig70.Data4, ARRAYSIZE(PdbSig70.Data4)
                        )
                    );
        pModInfo = Py_BuildValue(
                       "{s:k, s:K, s:k, s:k, s:k, s:k, s:i, s:s, s:s, s:s, s:s, "
                       " s:k, s:s, s:k, s:N, s:k, s:N, s:N, s:N, s:N, s:N, s:N, s:N}",
                       "SizeOfStruct", modInfo->SizeOfStruct,
                       "BaseOfImage", modInfo->BaseOfImage,
                       "ImageSize", modInfo->TimeDateStamp,
                       "TimeDateStamp", modInfo->TimeDateStamp,
                       "CheckSum", modInfo->CheckSum,
                       "NumSyms", modInfo->NumSyms,
                       "SymType", modInfo->SymType,
                       "ModuleName", modInfo->ModuleName,
                       "ImageName", modInfo->ImageName,
                       "LoadedImageName", modInfo->LoadedImageName,
                       "LoadedPdbName", modInfo->LoadedPdbName,
                       "CVSig", modInfo->CVSig,
                       "CVData", modInfo->CVData,
                       "PdbSig", modInfo->PdbSig,
                       "PdbSig70", pPdbSig70,
                       "PdbAge", modInfo->PdbAge,
                       "PdbUnmatched", PyBool_FromLong(modInfo->PdbUnmatched),
                       "DbgUnmatched", PyBool_FromLong(modInfo->DbgUnmatched),
                       "LineNumbers", PyBool_FromLong(modInfo->LineNumbers),
                       "GlobalSymbols", PyBool_FromLong(modInfo->GlobalSymbols),
                       "TypeInfo", PyBool_FromLong(modInfo->TypeInfo),
                       "SourceIndexed", PyBool_FromLong(modInfo->SourceIndexed),
                       "Publics", PyBool_FromLong(modInfo->Publics)
                   );
        pFdProcessInfo = Py_BuildValue(
                             "{s:N, s:N, s:k, s:k}",
                             "hProcess", PyInt_FromSize_t((size_t)fdProcessInfo->hProcess),
                             "hThread", PyInt_FromSize_t((size_t)fdProcessInfo->hThread),
                             "dwProcessId", fdProcessInfo->dwProcessId,
                             "dwThreadId", fdProcessInfo->dwThreadId
                         );
        pKwargs = Py_BuildValue(
                      "{s:N, s:N, s:s, s:N}",
                      "CreateProcessInfo", pCreateProcessInfo,
                      "modInfo", pModInfo,
                      "DebugFileName", callbackInfo->DebugFileName,
                      "fdProcessInfo", pFdProcessInfo
                  );
        pValue = PyObject_Call(pFunc, PyTuple_New(0), pKwargs);
        Py_DECREF(pCreateProcessInfo);
        Py_DECREF(pPdbSig70);
        Py_DECREF(pModInfo);
        Py_DECREF(pFdProcessInfo);
        Py_DECREF(pKwargs);
        Py_DECREF(pFunc);
        if(pValue == NULL)
        {
            _plugin_logputs("[PYTHON] Could not use create_process function.");
            PyErr_PrintEx(0);
            return;
        }

        Py_DECREF(pValue);
    }
}





void pyInit(PLUG_INITSTRUCT* initStruct)
{
  //  _plugin_logprintf("[PYTHON] pluginHandle: %d\n", pluginHandle);
  //  if(!_plugin_registercommand(pluginHandle, "Python", cbPythonCommand, false))
   //     _plugin_logputs("[PYTHON] error registering the \"Python\" command!");
//    if(!_plugin_registercommand(pluginHandle, "OpenScript", cbOpenScriptCommand, false))
   //     _plugin_logputs("[PYTHON] error registering the \"OpenScript\" command!");

    // Initialize the python environment
    Py_Initialize();
    PyEval_InitThreads();

    // Import x64dbg_python
    pModule = PyImport_Import(PyString_FromString(module_name));
    if(pModule != NULL)
    {
        // Get Event Object
        pEventObject = PyObject_GetAttrString(pModule, event_object_name);
        if(pEventObject == NULL)
        {
            _plugin_logputs("[PYTHON] Could not find Event object.");
            PyErr_PrintEx(0);
        }
    }
    else
    {
       // _plugin_logputs("[PYTHON] Could not import " module_name ".");
        PyErr_PrintEx(0);
    }

    PyRun_SimpleString("from " module_name " import *\n");
}

void pyStop()
{
  //  _plugin_unregistercommand(pluginHandle, "Python");
   // _plugin_unregistercommand(pluginHandle, "OpenScript");
    _plugin_unregistercommand(pluginHandle, "Recorder");

    _plugin_menuclear(hMenu);
    _plugin_menuclear(hMenuDisasm);
    _plugin_menuclear(hMenuDump);
    _plugin_menuclear(hMenuStack);

    _plugin_unregistercallback(pluginHandle, CB_WINEVENT);
    _plugin_unregistercallback(pluginHandle, CB_INITDEBUG);
    _plugin_unregistercallback(pluginHandle, CB_BREAKPOINT);
    _plugin_unregistercallback(pluginHandle, CB_STOPDEBUG);
    _plugin_unregistercallback(pluginHandle, CB_CREATEPROCESS);
    _plugin_unregistercallback(pluginHandle, CB_EXITPROCESS);
    _plugin_unregistercallback(pluginHandle, CB_CREATETHREAD);
    _plugin_unregistercallback(pluginHandle, CB_EXITTHREAD);
    _plugin_unregistercallback(pluginHandle, CB_SYSTEMBREAKPOINT);
    _plugin_unregistercallback(pluginHandle, CB_LOADDLL);
    _plugin_unregistercallback(pluginHandle, CB_UNLOADDLL);

    // Properly ends the python environment
    Py_Finalize();
}



void pySetup()
{
    // Set Menu Icon
    ICONDATA pyIcon;
    HRSRC hRes = FindResourceW(hInst, MAKEINTRESOURCEW(IDB_PNG1), L"PNG");
    DWORD size = SizeofResource(hInst, hRes);
    HGLOBAL hMem = LoadResource(hInst, hRes);

    pyIcon.data = LockResource(hMem);
    pyIcon.size = size;
    _plugin_menuseticon(hMenu, &pyIcon);

    FreeResource(hMem);
    _plugin_menuaddentry(hMenu, MENU_RECORD, "Recorder"); // edit this (name in subwindow)
    _plugin_registercallback(pluginHandle, CB_CREATEPROCESS, cbCreateProcessCallback);
    _plugin_registercallback(pluginHandle, CB_EXITPROCESS, cbExitProcessCallback);
    _plugin_registercallback(pluginHandle, CB_CREATETHREAD, cbCreateThreadCallback);
    _plugin_registercallback(pluginHandle, CB_EXITTHREAD, cbExitThreadCallback);
    _plugin_registercallback(pluginHandle, CB_LOADDLL, cbLoadDllCallback);
    _plugin_registercallback(pluginHandle, CB_UNLOADDLL, cbUnloadDllCallback);
}