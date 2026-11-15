#define _CRT_SECURE_NO_WARNINGS
#include "ConsoleApi.h"
#include <iostream>
#include <cstdio>
#include <chrono>
#include <cstring>
using namespace std;

static string FormatErrorText(DWORD error)
{
	char buffer[256];

	DWORD length = FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
		NULL, error, 0, buffer, sizeof(buffer) - 1, NULL);

	if (length == 0)
	{
		sprintf_s(buffer, "код %lu", (unsigned long)error);
		return string(buffer);
	}

	while (length > 0 && (buffer[length - 1] == '\r' || buffer[length - 1] == '\n' ||
		buffer[length - 1] == ' ' || buffer[length - 1] == '.'))
	{
		length--;
	}

	buffer[length] = '\0';
	return string(buffer);
}

static DWORD RemainingMs(ULONGLONG deadline)
{
	ULONGLONG now = GetTickCount64();
	return (now >= deadline) ? 1 : (DWORD)(deadline - now);
}

string NormalizePipeName(const char* pipeName)
{
	string value = (pipeName == NULL) ? "" : pipeName;

	if (value.empty())
		return "\\\\.\\pipe\\ConcurrentServer";

	if (value.rfind("\\\\", 0) == 0)
		return value;

	return "\\\\.\\pipe\\" + value;
}

// Подключение с дедлайном. Дескриптор открывается с FILE_FLAG_OVERLAPPED,
// чтобы запись и чтение можно было ограничить по времени.
HANDLE ConnectConsoleTimeout(const char* pipeName, DWORD timeoutMs, DWORD& lastError)
{
	string fullName = NormalizePipeName(pipeName);
	lastError = 0;

	ULONGLONG deadline = GetTickCount64() + timeoutMs;

	while (true)
	{
		HANDLE pipe = CreateFileA(fullName.c_str(),
			GENERIC_READ | GENERIC_WRITE,
			FILE_SHARE_READ | FILE_SHARE_WRITE,
			NULL,
			OPEN_EXISTING,
			FILE_FLAG_OVERLAPPED,
			NULL);

		if (pipe != INVALID_HANDLE_VALUE)
		{
			DWORD mode = PIPE_READMODE_MESSAGE;
			if (!SetNamedPipeHandleState(pipe, &mode, NULL, NULL))
			{
				lastError = GetLastError();
				CloseHandle(pipe);
				return INVALID_HANDLE_VALUE;
			}

			return pipe;
		}

		lastError = GetLastError();

		if (lastError != ERROR_PIPE_BUSY)
			return INVALID_HANDLE_VALUE;

		DWORD remaining = RemainingMs(deadline);
		if (remaining <= 1)
			return INVALID_HANDLE_VALUE;

		if (!WaitNamedPipeA(fullName.c_str(), remaining))
			lastError = GetLastError();

		if (GetTickCount64() >= deadline)
			return INVALID_HANDLE_VALUE;
	}
}

HANDLE ConnectConsole(const char* pipeName)
{
	DWORD lastError = 0;
	return ConnectConsoleTimeout(pipeName, CONSOLE_DEFAULT_TIMEOUT_MS, lastError);
}

// Ожидание завершения overlapped-операции с ограничением по дедлайну.
// Возвращает true при успехе; при таймауте отменяет операцию (CancelIoEx).
static bool WaitForOverlapped(HANDLE pipe, OVERLAPPED& overlapped, ULONGLONG deadline,
DWORD& lastError, bool cancelOnTimeout)
{
lastError = 0;
DWORD transferred = 0;

BOOL completed = GetOverlappedResult(pipe, &overlapped, &transferred, FALSE);
if (completed)
	return true;

	lastError = GetLastError();
	if (lastError != ERROR_IO_INCOMPLETE)
		return false;

	DWORD wait = WaitForSingleObject(overlapped.hEvent, RemainingMs(deadline));

	if (wait == WAIT_OBJECT_0)
	{
		DWORD bytesTransferred = 0;
		BOOL ok = GetOverlappedResult(pipe, &overlapped, &bytesTransferred, FALSE);
		if (!ok)
			lastError = GetLastError();
		return ok != FALSE;
	}

	if (cancelOnTimeout)
		CancelIoEx(pipe, &overlapped);

	lastError = ERROR_TIMEOUT;
	return false;
}

bool ExecuteConsoleCommand(const char* pipeName, const string& command,
	DWORD timeoutMs, ConsoleResult& result)
{
	result = ConsoleResult();
	ULONGLONG deadline = GetTickCount64() + timeoutMs;

	DWORD connectError = 0;
	HANDLE pipe = ConnectConsoleTimeout(pipeName, timeoutMs, connectError);

	if (pipe == INVALID_HANDLE_VALUE)
	{
		result.status = CONSOLE_CONNECT_FAILED;
		result.operation = "CreateFile";
		result.lastError = connectError;
		result.errorText = FormatErrorText(connectError);
		return false;
	}

	HANDLE event = CreateEventA(NULL, TRUE, FALSE, NULL);
	bool success = false;

	if (event == NULL)
	{
		result.status = CONSOLE_WRITE_FAILED;
		result.operation = "CreateEvent";
		result.lastError = GetLastError();
		result.errorText = FormatErrorText(result.lastError);
		CloseHandle(pipe);
		return false;
	}

	// запись команды; операция ограничена общим дедлайном
	OVERLAPPED writeOverlapped;
	memset(&writeOverlapped, 0, sizeof(writeOverlapped));
	writeOverlapped.hEvent = event;

	DWORD writeError = 0;
	BOOL written = WriteFile(pipe, command.c_str(), (DWORD)command.size(), NULL, &writeOverlapped);

	if (!written && GetLastError() != ERROR_IO_PENDING)
	{
		result.status = CONSOLE_WRITE_FAILED;
		result.operation = "WriteFile";
		result.lastError = GetLastError();
		result.errorText = FormatErrorText(result.lastError);
	}
	else
	{
		if (WaitForOverlapped(pipe, writeOverlapped, deadline, writeError, false))
		{
			// чтение ответа; при отсутствии ответа — таймаут, отмена чтения
			OVERLAPPED readOverlapped;
			memset(&readOverlapped, 0, sizeof(readOverlapped));
			readOverlapped.hEvent = event;

			char buffer[CONSOLE_ANSWER_SIZE];
			memset(buffer, 0, sizeof(buffer));
			DWORD bytesRead = 0;

			BOOL read = ReadFile(pipe, buffer, sizeof(buffer) - 1, &bytesRead, &readOverlapped);

			if (!read && GetLastError() != ERROR_IO_PENDING)
			{
				result.status = CONSOLE_READ_FAILED;
				result.operation = "ReadFile";
				result.lastError = GetLastError();
				result.errorText = FormatErrorText(result.lastError);
			}
			else
			{
				DWORD readError = 0;
				DWORD transferred = 0;

				if (WaitForOverlapped(pipe, readOverlapped, deadline, readError, true) &&
					GetOverlappedResult(pipe, &readOverlapped, &transferred, FALSE))
				{
					if (transferred > 0)
					{
						result.payload.assign(buffer, transferred);
						while (!result.payload.empty() && result.payload.back() == '\0')
							result.payload.pop_back();

						result.status = CONSOLE_OK;
						success = true;
					}
					else
					{
						result.status = CONSOLE_READ_FAILED;
						result.operation = "ReadFile";
						result.lastError = ERROR_NO_DATA;
						result.errorText = FormatErrorText(ERROR_NO_DATA);
					}
				}
				else if (readError == ERROR_TIMEOUT)
				{
					result.status = CONSOLE_TIMEOUT;
					result.operation = "ReadFile";
					result.lastError = ERROR_TIMEOUT;
					result.errorText = FormatErrorText(ERROR_TIMEOUT);
				}
				else
				{
					result.status = CONSOLE_READ_FAILED;
					result.operation = "ReadFile";
					result.lastError = readError;
					result.errorText = FormatErrorText(readError);
				}
			}
		}
		else if (writeError == ERROR_TIMEOUT)
		{
			result.status = CONSOLE_TIMEOUT;
			result.operation = "WriteFile";
			result.lastError = ERROR_TIMEOUT;
			result.errorText = FormatErrorText(ERROR_TIMEOUT);
		}
		else
		{
			result.status = CONSOLE_WRITE_FAILED;
			result.operation = "WriteFile";
			result.lastError = writeError;
			result.errorText = FormatErrorText(writeError);
		}
	}

	if (!success)
	{
		// прервать незавершённые операции перед закрытием дескриптора
		CancelIoEx(pipe, NULL);
	}

	CloseHandle(event);
	CloseHandle(pipe);
	return success;
}

bool SendConsoleCommand(const char* pipeName, const string& command, string& answer)
{
	ConsoleResult result;
	bool success = ExecuteConsoleCommand(pipeName, command, CONSOLE_DEFAULT_TIMEOUT_MS, result);

	answer.clear();
	if (success)
		answer = result.payload;

	return success;
}

bool IsKnownCommand(const string& command)
{
	return command == "start" || command == "stop" || command == "exit" ||
		command == "statistics" || command == "wait" || command == "shutdown";
}

string TrimCommand(const string& command)
{
	size_t begin = command.find_first_not_of(" \t\r\n");
	if (begin == string::npos)
		return "";

	size_t end = command.find_last_not_of(" \t\r\n");
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
