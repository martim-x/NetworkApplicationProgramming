#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <string>
#include <iostream>
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
	const char* mailslotName = (argc > 1) ? argv[1] : "\\\\.\\mailslot\\Box";

	HANDLE hMailslot;
	WSADATA wsaData;
	char receiveBuffer[512];
	DWORD bytesRead;
	clock_t startTime, endTime;
	bool firstMessage = true;

	try
	{
		if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
			throw SetErrorMsgText("Startup:", WSAGetLastError());

		cout << "ServerMS: Creating mailslot " << mailslotName << "..." << endl;

		// Создание мейлслота:
		// 0 - максимальный размер сообщения (0 = неограничен, до 424 байт для датаграмм)
		// 1800000 - таймаут ожидания чтения (30 минут в мс), 0 = неограничен, MAILSLOT_WAIT_FOREVER = INFINITE
		if ((hMailslot = CreateMailslotA(mailslotName,
			0,                          // максимальный размер сообщения
			MAILSLOT_WAIT_FOREVER,      // таймаут ожидания (INFINITE)
			NULL)) == INVALID_HANDLE_VALUE) // атрибуты безопасности
			throw SetErrorMsgText("CreateMailslot:", GetLastError());

		cout << "ServerMS: Waiting for messages on mailslot " << mailslotName << "..." << endl;

		while (true)
		{
			if (!ReadFile(hMailslot, receiveBuffer, sizeof(receiveBuffer) - 1, &bytesRead, NULL))
				throw SetErrorMsgText("ReadFile:", GetLastError());

			if (bytesRead == 0)
				continue;

			receiveBuffer[bytesRead] = '\0';

			if (firstMessage)
			{
				startTime = clock();
				firstMessage = false;
			}

			// Проверка сигнала завершения
			if (strcmp(receiveBuffer, "STOP") == 0)
			{
				endTime = clock();
				cout << "\nReceived STOP signal." << endl;
				cout << "Time for send and recv: " << ((double)(endTime - startTime)) / CLOCKS_PER_SEC << " seconds.\n";
				firstMessage = true; // готовимся к следующей сессии
				continue;
			}

			cout << "Received: " << receiveBuffer << endl;
		}

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