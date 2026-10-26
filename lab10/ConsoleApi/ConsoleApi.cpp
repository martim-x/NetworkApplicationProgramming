#define _CRT_SECURE_NO_WARNINGS
#include "ConsoleApi.h"
#include <iostream>
#include <cstring>
using namespace std;

HANDLE ConnectConsole(const char* pipeName)
{
	for (int attempt = 0; attempt < 50; attempt++)
	{
		HANDLE hPipe = CreateFileA(pipeName,
			GENERIC_READ | GENERIC_WRITE,
			FILE_SHARE_READ | FILE_SHARE_WRITE,
			NULL,
			OPEN_EXISTING,
			0,
			NULL);

		if (hPipe != INVALID_HANDLE_VALUE)
		{
			DWORD mode = PIPE_READMODE_MESSAGE;
			if (!SetNamedPipeHandleState(hPipe, &mode, NULL, NULL))
			{
				CloseHandle(hPipe);
				return INVALID_HANDLE_VALUE;
			}

			return hPipe;
		}

		if (GetLastError() != ERROR_PIPE_BUSY)
			return INVALID_HANDLE_VALUE;

		if (!WaitNamedPipeA(pipeName, 500))
			Sleep(100);
	}

	return INVALID_HANDLE_VALUE;
}

bool SendConsoleCommand(const char* pipeName, const string& command, string& answer)
{
	HANDLE pipe = ConnectConsole(pipeName);
	if (pipe == INVALID_HANDLE_VALUE)
		return false;

	DWORD bytesWritten = 0;
	DWORD bytesRead = 0;
	char buffer[CONSOLE_ANSWER_SIZE];

	bool result = false;

	if (WriteFile(pipe, command.c_str(), (DWORD)command.size(), &bytesWritten, NULL) &&
		FlushFileBuffers(pipe))
	{
		memset(buffer, 0, sizeof(buffer));

		if (ReadFile(pipe, buffer, sizeof(buffer) - 1, &bytesRead, NULL) && bytesRead > 0)
		{
			buffer[bytesRead] = '\0';
			answer = buffer;
			result = true;
		}
	}

	CloseHandle(pipe);
	return result;
}

bool IsKnownCommand(const string& command)
{
	return command == "start" || command == "stop" || command == "exit" ||
		command == "statistics" || command == "wait" || command == "shutdown";
}

string TrimCommand(const string& command)
{
	size_t begin = command.find_first_not_of(" 	\r\n");
	if (begin == string::npos)
		return "";

	size_t end = command.find_last_not_of(" 	\r\n");
	string result = command.substr(begin, end - begin + 1);

	if (result.size() >= 3 &&
		(unsigned char)result[0] == 0xEF &&
		(unsigned char)result[1] == 0xBB &&
		(unsigned char)result[2] == 0xBF)
		result = result.substr(3);

	return result;
}

void PrintCommandHelp()
{
	cout << "Команды консоли сервера ConcurrentServer:" << endl;
	cout << "  start       — разрешить подключение клиентов" << endl;
	cout << "  stop        — запретить подключение клиентов" << endl;
	cout << "  statistics  — вывести статистику сервера" << endl;
	cout << "  wait        — запретить приём и дождаться обслуживания клиентов" << endl;
	cout << "  shutdown    — дождаться клиентов и завершить сервер" << endl;
	cout << "  exit        — завершить сервер" << endl;
	cout << "  help        — эта справка" << endl;
	cout << "  quit        — выйти из консоли (сервер продолжит работу)" << endl;
}
