#define _WINSOCK_DEPRECATED_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#include <winsock2.h>
#include <windows.h>
#include <iostream>
#include <string>
#include <cstdio>
#include <cstring>
#include <cstdlib>
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

	string pipeName = BuildPipeName((argc > 1) ? argv[1] : NULL);
	int messageTotal = (argc > 2) ? atoi(argv[2]) : 0;

	HANDLE hPipe;
	WSADATA wsaData;
	char sendBuffer[512];
	char receiveBuffer[512];

	try
	{
		if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
			throw SetErrorMsgText("Startup: ", WSAGetLastError());

		cout << "ClientNPt: подключение к каналу " << pipeName << endl;

		if ((hPipe = CreateFileA(pipeName.c_str(),
			GENERIC_READ | GENERIC_WRITE,
			FILE_SHARE_READ | FILE_SHARE_WRITE,
			NULL,
			OPEN_EXISTING,
			0,
			NULL)) == INVALID_HANDLE_VALUE)
		{
			DWORD err = GetLastError();

			if (err == ERROR_PIPE_BUSY)
			{
				cout << "Все экземпляры канала заняты, ожидание..." << endl;
				if (!WaitNamedPipeA(pipeName.c_str(), 5000))
					throw SetErrorMsgText("WaitNamedPipe: ", GetLastError());

				if ((hPipe = CreateFileA(pipeName.c_str(),
					GENERIC_READ | GENERIC_WRITE,
					FILE_SHARE_READ | FILE_SHARE_WRITE,
					NULL,
					OPEN_EXISTING,
					0,
					NULL)) == INVALID_HANDLE_VALUE)
					throw SetErrorMsgText("CreateFile: ", GetLastError());
			}
			else
				throw SetErrorMsgText("CreateFile: ", err);
		}

		cout << "Соединение с сервером установлено" << endl;

		DWORD readMode = PIPE_READMODE_MESSAGE;
		if (!SetNamedPipeHandleState(hPipe, &readMode, NULL, NULL))
			throw SetErrorMsgText("SetNamedPipeHandleState: ", GetLastError());

		if (messageTotal <= 0)
		{
			cout << "Введите количество транзакций: ";
			cin >> messageTotal;
		}

		clock_t startTime = clock();

		for (int i = 1; i <= messageTotal; i++)
		{
			sprintf(sendBuffer, "Hello from ClientNPt %03d", i);

			DWORD bytesRead = 0;
			if (!TransactNamedPipe(hPipe,
				sendBuffer, (DWORD)(strlen(sendBuffer) + 1),
				receiveBuffer, sizeof(receiveBuffer) - 1,
				&bytesRead, NULL))
				throw SetErrorMsgText("TransactNamedPipe: ", GetLastError());

			receiveBuffer[bytesRead] = '\0';
			cout << "Получено: " << receiveBuffer << endl;
		}

		clock_t endTime = clock();

		cout << "Время " << messageTotal << " транзакций: "
			<< ((double)(endTime - startTime)) / CLOCKS_PER_SEC << " с" << endl;

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
