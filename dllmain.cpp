// dllmain.cpp : Defines the entry point for the DLL application.
#include "pch.h"
#include "common.h"
#include "errorCodes.h"
#include "filemgr.h"
#include "value.h"

using namespace std;

// Globals
filext::filemgr gFileMgr;
string dllFolder;
string fileStorageFolder;

bool checkFileName(string& fileName);
string getDllFolder();

BOOL APIENTRY DllMain( HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
                     )
{
    switch (ul_reason_for_call)
    {

    case DLL_PROCESS_ATTACH:
		// Allocate console
#ifdef _DEBUG
		::AllocConsole();
		freopen_s((FILE**)stdout, "CONOUT$", "w", stdout);
		LOG(("DLL Attached\n"));
#endif
		// Resolve path to this dll
		dllFolder = getDllFolder();
		LOG(("DLL path: %s\n", dllFolder.c_str()));
		fileStorageFolder = dllFolder + string("storage\\");

		// Ensure that there is a folder for filext files
		if (!filesystem::exists(fileStorageFolder)) {
			filesystem::create_directory(fileStorageFolder);
		}
		break;



    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}

extern "C"
{
	__declspec(dllexport) void __stdcall RVExtension(char* output, int outputSize, const char* function);
	__declspec(dllexport) int __stdcall RVExtensionArgs(char* output, int outputSize, const char* function, const char** argv, int argc);
	__declspec(dllexport) void __stdcall RVExtensionVersion(char* output, int outputSize);
}

void RVExtension(char* output, int outputSize, const char* function)
{
	strcpy_s(output, outputSize-1, function);
	LOG(("Function called: %s\n", function));
}

// Macro for asserting correct argument count
#define ASSERT_EXT_ARGC(argc, argcNeeded) if (argc != argcNeeded)	{ \
LOG(("Wrong arg count, received: %i, expected: %i\n", argc, argcNeeded)); \
return FILEXT_ERROR_WRONG_ARG_COUNT; \
}

/*
We use function argument to pass actual data,
because all strings passed through argv are wrapped into extra quotes - they are stringified strings :/ :/ :/

callExtension arguments are:
[(value), [function, (fileName), (key)]]
*/
int RVExtensionArgs(char* output, int outputSize, const char* function, const char** argv, int argc)
{
	// Extract function name
	const char* functionName = argv[0];
	const char* data = function; // It doesn't have the quotes!

	// Exctract file name: remove leading and trailing "
	string fileName("");
	if (argc >= 2) {
		fileName = string(argv[1]);
		fileName.erase(0, 1);
		fileName.pop_back();
		//LOG(("Argc: %i\n", argc));

		// Check file name
		// Bail if file name is wrong
		if (!checkFileName(fileName))
			return FILEXT_ERROR_WRONG_FILE_NAME;
		fileName = fileStorageFolder + fileName;
	}

	LOG(("RVExtensionArgs: function: %s, fileName: %s, outputSize: %i\n", functionName, fileName.c_str(), outputSize));

	// Resolve function name

	// ["", ["open", fileName]]
	if (strcmp(functionName, "\"open\"") == 0) {
		ASSERT_EXT_ARGC(argc, 2);
		return gFileMgr.open(fileName);
	};

	// ["", ["close", fileName]]
	if (strcmp(functionName, "\"close\"") == 0) {
		ASSERT_EXT_ARGC(argc, 2);
		return gFileMgr.close(fileName);
	};

	// ["", ["write", fileName]]
	if (strcmp(functionName, "\"write\"") == 0) {
		ASSERT_EXT_ARGC(argc, 2);
		return gFileMgr.write(fileName);
	};

	// ["", ["read", fileName]]
	if (strcmp(functionName, "\"read\"") == 0) {
		ASSERT_EXT_ARGC(argc, 2);
		return gFileMgr.read(fileName);
	};

	// ["", ["get", fileName, key, reset(0/1)]]
	if (strcmp(functionName, "\"get\"") == 0) {
		ASSERT_EXT_ARGC(argc, 4);
		outputSize -= 4; // Just to be safe...
		std::string strOut("");
		int reset = 0;
		try {
			reset = std::stoi(argv[3]);
		} catch ( ... ) {
			reset = 0;
		}
		int retInt = gFileMgr.get(fileName, argv[2], strOut, outputSize, (bool)reset);
		strcpy_s(output, outputSize - 1, strOut.c_str());
		return retInt;
	};

	// [value, ["set", fileName, key]]
	if (strcmp(functionName, "\"set\"") == 0) {
		ASSERT_EXT_ARGC(argc, 3);
		return gFileMgr.set(fileName, argv[2], data);
	};

	// ["", ["getFiles"]]
	if (strcmp(functionName, "\"getFiles\"") == 0) {
		ASSERT_EXT_ARGC(argc, 1);

		vector<sqf::value> vectorFileNamesSQF;
		for (const auto& entry : filesystem::directory_iterator(fileStorageFolder)) {
			string fileName = entry.path().filename().string();
			vectorFileNamesSQF.push_back(sqf::value(fileName));
			LOG(("File: %s\n", fileName.c_str()));
		}
		
		sqf::value fileNamesSQFArray(vectorFileNamesSQF);
		string strOut = fileNamesSQFArray.to_string();

		strcpy_s(output, outputSize - 1, strOut.c_str());
		return 0;
	};

	// ["", ["read", fileName]]
	if (strcmp(functionName, "\"deleteFile\"") == 0) {
		try {
			filesystem::remove(fileName);
		} catch (...) {
			return FILEXT_ERROR_WRONG_FILE_NAME;
		}
		return FILEXT_SUCCESS;
	};

	// ["testDataToCopy", ["loopback"]]
	if (strcmp(functionName, "\"loopback\"") == 0) {
		ASSERT_EXT_ARGC(argc, 1)
		strcpy_s(output, outputSize-1, data);
		return 0;
	};

	// ["testData", ["dummy"]]
	if (strcmp(functionName, "\"dummy\"") == 0) {
		ASSERT_EXT_ARGC(argc, 1)
		LOG(("Data length: %i", (int)strlen(data)));
		return (int)strlen(data);
	};

	return FILEXT_ERROR_WRONG_FUNCTION_NAME;
}

void RVExtensionVersion(char* output, int outputSize)
{
	strcpy_s(output, outputSize-1, "Filext 1.0");
}

// Ensures that the file name is correct:
// Has non-negative length
// Doesn't have / or \ inside it
bool checkFileName(string& fileName) {

	// Check length
	if (fileName.size() == 0)
		return false;

	// Check forbidden characters
	string forbiddenChars("\\/?%*:|\"<>,;=");
	if (fileName.find_first_of(forbiddenChars) != string::npos)
		return false;

	// All good so far
	return true;
}


// Borrowed from https://gist.github.com/pwm1234/05280cf2e462853e183d
std::string getDllFolder()
{
	char path[FILENAME_MAX];
	HMODULE hm = NULL;

	if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
		GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
		(LPCSTR)getDllFolder,
		&hm))
	{
		LOG(("error: GetModuleHandle returned %i", GetLastError()));
		return std::string("");
	}

	GetModuleFileNameA(hm, path, sizeof(path));
	std::string p(path);

	// Remove DLL name from the path
	auto pos = p.rfind('\\');
	p.erase(pos + 1, p.size());
	return p;
}