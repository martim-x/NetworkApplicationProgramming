#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <string>
#include <iostream>
#include <cstdlib>
#include <ctime>
#include <Windows.h>
#include <map>
#pragma comment(lib, "WS2_32.lib")
using namespace std;

string GetErrorMsgText(int code)
{
	switch (code)
	{
	case WSAEINTR:          return "WSAEINTR: Работа функции прервана";
	case WSAEACCES:         return "WSAEACCES: Разрешение отвергнуто";
	case WSAEFAULT:         return "WSAEFAULT: Ошибочный адрес";
	case WSAEINVAL:         return "WSAEINVAL: Ошибка в аргументе";
	case WSAEMFILE:         return "WSAEMFILE: Слишком много файлов открыто";
	case WSAENOTSOCK:       return "WSAENOTSOCK: Сокет задан неправильно";
	case WSAEADDRINUSE:     return "WSAEADDRINUSE: Адрес уже используется";
	case WSAEADDRNOTAVAIL:  return "WSAEADDRNOTAVAIL: Запрошенный адрес не может быть использован";
	case WSAENETDOWN:       return "WSAENETDOWN: Сеть отключена";
	case WSAENETRESET:      return "WSAENETRESET: Сеть разорвала соединение";
	case WSAECONNABORTED:   return "WSAECONNABORTED: Программный отказ связи";
	case WSAECONNRESET:     return "WSAECONNRESET: Соединение сброшено";
	case WSAENOTCONN:       return "WSAENOTCONN: Сокет не подключен";
	case WSAETIMEDOUT:      return "WSAETIMEDOUT: Закончился отведенный интервал времени";
	case WSAECONNREFUSED:   return "WSAECONNREFUSED: Соединение отклонено";
	case WSANOTINITIALISED: return "WSANOTINITIALISED: Не выполнена инициализация WS2_32.DLL";
	case ERROR_INVALID_PARAMETER: return "ERROR_INVALID_PARAMETER: Неверный параметр";
	case ERROR_NO_DATA:     return "ERROR_NO_DATA: Канал закрывается";
	case ERROR_PIPE_CONNECTED: return "ERROR_PIPE_CONNECTED: Есть процесс на другом конце канала";
	case ERROR_PIPE_LISTENING: return "ERROR_PIPE_LISTENING: Ожидание процесса на другом конце";
	case ERROR_BROKEN_PIPE: return "ERROR_BROKEN_PIPE: Канал разорван";
	default:                return "Ошибка Named Pipe, код " + to_string(code);
	}
}

string SetErrorMsgText(string msgText, int code)
{
	return msgText + GetErrorMsgText(code);
}

int main(int argc, char* argv[])
{
	const char* pipeName = (argc > 1) ? argv[1] : "\\\\.\\pipe\\Tube";

	HANDLE hPipe;
	WSADATA wsaData;
	char buffer[512];
	DWORD bytesRead, bytesWritten;
	map<string, int> messageCounts;
	clock_t startTime, endTime;

	try
	{
		if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
			throw SetErrorMsgText("Startup:", WSAGetLastError());

		cout << "ServerNP: Creating pipe " << pipeName << "..." << endl;

		if ((hPipe = CreateNamedPipeA(pipeName,
			PIPE_ACCESS_DUPLEX,
			PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
			PIPE_UNLIMITED_INSTANCES,
			512,
			512,
			INFINITE,
			NULL)) == INVALID_HANDLE_VALUE)
			throw SetErrorMsgText("CreateNamedPipe:", GetLastError());

		cout << "ServerNP: Waiting for clients on pipe " << pipeName << "..." << endl;

		while (true)
		{
			if (!ConnectNamedPipe(hPipe, NULL))
			{
				DWORD err = GetLastError();
				if (err != ERROR_PIPE_CONNECTED)
					throw SetErrorMsgText("ConnectNamedPipe:", err);
			}

			cout << "\nClient connected." << endl;

			messageCounts["client"] = 0;
			startTime = clock();

			while (true)
			{
				if (!ReadFile(hPipe, buffer, sizeof(buffer) - 1, &bytesRead, NULL))
					throw SetErrorMsgText("ReadFile:", GetLastError());

				if (bytesRead == 0 || buffer[0] == '\0')
					break;

				buffer[bytesRead] = '\0';
				messageCounts["client"]++;
				cout << "Received: " << buffer << endl;

				if (!WriteFile(hPipe, buffer, bytesRead, &bytesWritten, NULL))
					throw SetErrorMsgText("WriteFile:", GetLastError());
			}

			endTime = clock();

			cout << "\nClient disconnected. Messages: " << messageCounts["client"] << endl;
			cout << "Exchange time: " << ((double)(endTime - startTime)) / CLOCKS_PER_SEC
				<< " seconds.\n" << endl;

			DisconnectNamedPipe(hPipe);
			cout << "Waiting for the next client...\n" << endl;
		}

		if (!CloseHandle(hPipe))
			throw SetErrorMsgText("CloseHandle:", GetLastError());

		if (WSACleanup() == SOCKET_ERROR)
			throw SetErrorMsgText("Cleanup:", WSAGetLastError());
	}
	catch (string errorMsgText)
	{
		cout << endl << "WSAGetLastError: " << errorMsgText;
	}

	return 0;
}