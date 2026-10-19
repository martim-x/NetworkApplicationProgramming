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
	case ERROR_FILE_NOT_FOUND: return "ERROR_FILE_NOT_FOUND: Мейлслот не найден";
	case ERROR_INVALID_NAME: return "ERROR_INVALID_NAME: Неверное имя мейлслота";
	default:                return "Ошибка Mailslot, код " + to_string(code);
	}
}

string SetErrorMsgText(string msgText, int code)
{
	return msgText + GetErrorMsgText(code);
}

int main(int argc, char* argv[])
{
	const char* mailslotName = (argc > 1) ? argv[1] : "\\\\*\\mailslot\\Box";

	HANDLE hMailslot;
	WSADATA wsaData;
	char sendBuffer[512];
	DWORD bytesWritten;
	int messageTotal;
	clock_t startTime, endTime;

	try
	{
		if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
			throw SetErrorMsgText("Startup:", WSAGetLastError());

		cout << "ClientMS: Connecting to mailslot " << mailslotName << "..." << endl;

		if ((hMailslot = CreateFileA(mailslotName,
			GENERIC_WRITE,
			FILE_SHARE_READ,
			NULL,
			OPEN_EXISTING,
			FILE_ATTRIBUTE_NORMAL,
			NULL)) == INVALID_HANDLE_VALUE)
			throw SetErrorMsgText("CreateFile:", GetLastError());

		cout << "Connected to mailslot." << endl;

		cout << "Enter number of messages: ";
		cin >> messageTotal;

		strcpy(sendBuffer, "Hello from ClientMS");

		startTime = clock();
		for (int i = 1; i <= messageTotal; i++)
		{
			if (!WriteFile(hMailslot, sendBuffer, (DWORD)(strlen(sendBuffer) + 1), &bytesWritten, NULL))
				throw SetErrorMsgText("WriteFile:", GetLastError());

			cout << "Sent [" << i << "]: " << sendBuffer << endl;
		}
		endTime = clock();

		// Отправка сигнала завершения
		const char* stopMsg = "STOP";
		if (!WriteFile(hMailslot, stopMsg, (DWORD)(strlen(stopMsg) + 1), &bytesWritten, NULL))
			throw SetErrorMsgText("WriteFile:", GetLastError());

		cout << "\nExchange time for " << messageTotal << " messages: "
			<< ((double)(endTime - startTime)) / CLOCKS_PER_SEC << " seconds.\n";

		if (!CloseHandle(hMailslot))
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