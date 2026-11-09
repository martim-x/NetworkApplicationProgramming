#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <winsock2.h>
#include <windows.h>
#include <iostream>
#include <string>
#include <ctime>
#pragma comment(lib, "WS2_32.lib")
using namespace std;

string GetErrorMsgText(int code)
{
	switch (code)
	{
	case WSAEINTR:                 return "WSAEINTR: Работа функции прервана";
	case WSAEACCES:                return "WSAEACCES: Разрешение отвергнуто";
	case WSAEFAULT:                return "WSAEFAULT: Ошибочный адрес";
	case WSAEINVAL:                return "WSAEINVAL: Ошибка в аргументе";
	case WSAEMFILE:                return "WSAEMFILE: Слишком много файлов открыто";
	case WSAENOTSOCK:              return "WSAENOTSOCK: Сокет задан неправильно";
	case WSAEADDRINUSE:            return "WSAEADDRINUSE: Адрес уже используется";
	case WSAENETDOWN:              return "WSAENETDOWN: Сеть отключена";
	case WSAENETRESET:             return "WSAENETRESET: Сеть разорвала соединение";
	case WSAECONNABORTED:          return "WSAECONNABORTED: Программный отказ связи";
	case WSAECONNRESET:            return "WSAECONNRESET: Соединение сброшено";
	case WSAENOTCONN:              return "WSAENOTCONN: Сокет не подключен";
	case WSAETIMEDOUT:             return "WSAETIMEDOUT: Закончился отведенный интервал времени";
	case WSAECONNREFUSED:          return "WSAECONNREFUSED: Соединение отклонено";
	case WSANOTINITIALISED:        return "WSANOTINITIALISED: Не выполнена инициализация WS2_32.DLL";
	case ERROR_INVALID_PARAMETER:  return "ERROR_INVALID_PARAMETER: Неверный параметр";
	case ERROR_FILE_NOT_FOUND:     return "ERROR_FILE_NOT_FOUND: Канал с таким именем не создан";
	case ERROR_ACCESS_DENIED:      return "ERROR_ACCESS_DENIED: Нет прав доступа к каналу";
	case ERROR_PIPE_BUSY:          return "ERROR_PIPE_BUSY: Все экземпляры канала заняты";
	case ERROR_NO_DATA:            return "ERROR_NO_DATA: Канал закрывается";
	case ERROR_PIPE_CONNECTED:     return "ERROR_PIPE_CONNECTED: Есть процесс на другом конце канала";
	case ERROR_PIPE_LISTENING:     return "ERROR_PIPE_LISTENING: Ожидание процесса на другом конце";
	case ERROR_PIPE_NOT_CONNECTED: return "ERROR_PIPE_NOT_CONNECTED: Нет процесса на другом конце канала";
	case ERROR_BROKEN_PIPE:        return "ERROR_BROKEN_PIPE: Канал разорван";
	case ERROR_MORE_DATA:          return "ERROR_MORE_DATA: Сообщение не поместилось в буфер";
	default:                       return "Ошибка Named Pipe, код " + to_string(code);
	}
}

string SetErrorMsgText(string msgText, int code)
{
	return msgText + GetErrorMsgText(code);
}

string BuildPipeName(const char* argument)
{
	string value = (argument == NULL) ? "" : argument;

	if (value.empty())
		return "\\\\.\\pipe\\Tube";

	if (value.rfind("\\\\", 0) == 0)
		return value;

	return "\\\\.\\pipe\\" + value;
}

int main(int argc, char* argv[])
{
	SetConsoleOutputCP(CP_UTF8);

	string pipeNameText = BuildPipeName((argc > 1) ? argv[1] : NULL);

	if (pipeNameText.rfind("\\\\.\\", 0) != 0)
	{
		cout << "Ошибка: сервер создаёт канал только на локальном компьютере, "
			"нужно \\\\.\\pipe\\Имя, получено: " << pipeNameText << endl;
		return 1;
	}

	const char* pipeName = pipeNameText.c_str();
	char buffer[512];
	HANDLE hPipe;
	WSADATA wsaData;
	int clientCount = 0;

	try
	{
		if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
			throw SetErrorMsgText("Startup: ", WSAGetLastError());

		if ((hPipe = CreateNamedPipeA(pipeName,
			PIPE_ACCESS_DUPLEX,
			PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
			PIPE_UNLIMITED_INSTANCES,
			512,
			512,
			INFINITE,
			NULL)) == INVALID_HANDLE_VALUE)
			throw SetErrorMsgText("CreateNamedPipe: ", GetLastError());

		cout << "ServerNP: канал создан: " << pipeName << endl;

		while (true)
		{
			cout << "ServerNP: ожидание подключения клиента..." << endl;

			if (!ConnectNamedPipe(hPipe, NULL))
			{
				DWORD err = GetLastError();
				if (err != ERROR_PIPE_CONNECTED)
					throw SetErrorMsgText("ConnectNamedPipe: ", err);
			}

			clientCount++;
			cout << "Клиент №" << clientCount << " подключён" << endl;

			int messageCount = 0;
			clock_t startTime = clock();

			while (true)
			{
				DWORD bytesRead = 0;

				if (!ReadFile(hPipe, buffer, sizeof(buffer), &bytesRead, NULL))
				{
					DWORD err = GetLastError();
					if (err == ERROR_BROKEN_PIPE || err == ERROR_NO_DATA ||
						err == ERROR_PIPE_NOT_CONNECTED)
						break;
					throw SetErrorMsgText("ReadFile: ", err);
				}

				if (bytesRead == 0 || buffer[0] == '\0')
					break;

				if (bytesRead < sizeof(buffer))
					buffer[bytesRead] = '\0';

				messageCount++;
				cout << "  Получено: " << buffer << endl;

				DWORD bytesWritten = 0;
				if (!WriteFile(hPipe, buffer, bytesRead, &bytesWritten, NULL))
				{
					DWORD err = GetLastError();
					if (err == ERROR_BROKEN_PIPE || err == ERROR_NO_DATA)
						break;
					throw SetErrorMsgText("WriteFile: ", err);
				}
			}

			clock_t endTime = clock();

			cout << "Клиент №" << clientCount << " отключён. Сообщений: " << messageCount << endl;
			cout << "Время обмена: " << ((double)(endTime - startTime)) / CLOCKS_PER_SEC
				<< " с" << endl;

			if (!DisconnectNamedPipe(hPipe))
				throw SetErrorMsgText("DisconnectNamedPipe: ", GetLastError());
		}

		if (!CloseHandle(hPipe))
			throw SetErrorMsgText("CloseHandle: ", GetLastError());

		if (WSACleanup() == SOCKET_ERROR)
			throw SetErrorMsgText("Cleanup: ", WSAGetLastError());
	}
	catch (string errorMsgText)
	{
		cout << endl << "Ошибка: " << errorMsgText << endl;
		return 1;
	}

	return 0;
}
