#define _WINSOCK_DEPRECATED_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#include <string>
#include <iostream>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <Windows.h>
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
	char sendBuffer[512];
	char receiveBuffer[512];
	DWORD bytesWritten, bytesRead;
	int messageTotal;
	int messageNumber = 1;
	clock_t startTime, endTime;

	try
	{
		if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
			throw SetErrorMsgText("Startup:", WSAGetLastError());

		cout << "ClientNP: Connecting to pipe " << pipeName << "..." << endl;

		if ((hPipe = CreateFileA(pipeName,
			GENERIC_READ | GENERIC_WRITE,
			FILE_SHARE_READ | FILE_SHARE_WRITE,
			NULL,
			OPEN_EXISTING,
			0,
			NULL)) == INVALID_HANDLE_VALUE)
			throw SetErrorMsgText("CreateFile:", GetLastError());

		cout << "Connected to server." << endl;

		cout << "Enter number of messages: ";
		cin >> messageTotal;

		startTime = clock();
		for (int i = 0; i < messageTotal; i++)
		{
			sprintf(sendBuffer, "Hello from ClientNP %03d", messageNumber);

			if (!WriteFile(hPipe, sendBuffer, (DWORD)(strlen(sendBuffer) + 1), &bytesWritten, NULL))
				throw SetErrorMsgText("WriteFile:", GetLastError());

			if (!ReadFile(hPipe, receiveBuffer, sizeof(receiveBuffer) - 1, &bytesRead, NULL))
				throw SetErrorMsgText("ReadFile:", GetLastError());

			receiveBuffer[bytesRead] = '\0';
			cout << "Received: " << receiveBuffer << endl;

			messageNumber++;
		}
		endTime = clock();

		cout << "\nExchange time for " << messageTotal << " messages: "
			<< ((double)(endTime - startTime)) / CLOCKS_PER_SEC << " seconds.\n";

		if (!WriteFile(hPipe, "", 1, &bytesWritten, NULL))
			throw SetErrorMsgText("WriteFile:", GetLastError());

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