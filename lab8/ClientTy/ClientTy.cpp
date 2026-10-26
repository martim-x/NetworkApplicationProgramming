#define _WINSOCK_DEPRECATED_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#include <winsock2.h>
#include <windows.h>
#include <iostream>
#include <string>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#pragma comment(lib, "WS2_32.lib")
using namespace std;

static string GetErrorMsgText(int code)
{
	switch (code)
	{
	case WSAEINTR:              return "WSAEINTR: Работа функции прервана";
	case WSAEACCES:             return "WSAEACCES: Разрешение отвергнуто";
	case WSAEFAULT:             return "WSAEFAULT: Ошибочный адрес";
	case WSAEINVAL:             return "WSAEINVAL: Ошибка в аргументе";
	case WSAEMFILE:             return "WSAEMFILE: Слишком много файлов открыто";
	case WSAENOTSOCK:           return "WSAENOTSOCK: Сокет задан неправильно";
	case WSAEADDRINUSE:         return "WSAEADDRINUSE: Адрес уже используется";
	case WSAENETDOWN:           return "WSAENETDOWN: Сеть отключена";
	case WSAENETRESET:          return "WSAENETRESET: Сеть разорвала соединение";
	case WSAECONNABORTED:       return "WSAECONNABORTED: Программный отказ связи";
	case WSAECONNRESET:         return "WSAECONNRESET: Связь восстановлена";
	case WSAECONNREFUSED:       return "WSAECONNREFUSED: Соединение отклонено";
	case WSAETIMEDOUT:          return "WSAETIMEDOUT: Закончился отведенный интервал времени";
	case WSANOTINITIALISED:     return "WSANOTINITIALISED: Не выполнена инициализация WS2_32.DLL";
	default:                    return "Ошибка Winsock2, код " + to_string(code);
	}
}

int main(int argc, char* argv[])
{
	SetConsoleOutputCP(CP_UTF8);

	const char* serverIp = (argc > 1) ? argv[1] : "127.0.0.1";
	int serverPort = (argc > 2) ? atoi(argv[2]) : 2000;
	int cycles = (argc > 3) ? atoi(argv[3]) : 10;
	int delayMs = (argc > 4) ? atoi(argv[4]) : 100;

	WSADATA wsaData;
	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
	{
		cout << "ClientTy: Startup: " << GetErrorMsgText(WSAGetLastError()) << endl;
		return 1;
	}

	cout << "ClientTy: подключение/отключение " << cycles << " раз к "
		<< serverIp << ":" << serverPort << endl;

	int ok = 0;
	int failed = 0;
	DWORD start = GetTickCount();

	for (int i = 1; i <= cycles; i++)
	{
		SOCKET s = socket(AF_INET, SOCK_STREAM, 0);
		if (s == INVALID_SOCKET)
		{
			failed++;
			continue;
		}

		SOCKADDR_IN address;
		memset(&address, 0, sizeof(address));
		address.sin_family = AF_INET;
		address.sin_port = htons((short)serverPort);
		address.sin_addr.s_addr = inet_addr(serverIp);

		if (connect(s, (sockaddr*)&address, sizeof(address)) == SOCKET_ERROR)
		{
			cout << "  цикл " << i << ": connect: " << GetErrorMsgText(WSAGetLastError()) << endl;
			closesocket(s);
			failed++;
			Sleep(delayMs);
			continue;
		}

		DWORD timeout = 3000;
		setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));

		char buffer[256];
		sprintf_s(buffer, "echo");
		if (send(s, buffer, (int)strlen(buffer) + 1, 0) == SOCKET_ERROR)
		{
			failed++;
			closesocket(s);
			Sleep(delayMs);
			continue;
		}

		sprintf_s(buffer, "Hello from ClientTy %03d", i);
		if (send(s, buffer, (int)strlen(buffer) + 1, 0) == SOCKET_ERROR)
		{
			failed++;
			closesocket(s);
			Sleep(delayMs);
			continue;
		}

		char answer[256];
		memset(answer, 0, sizeof(answer));
		int bytes = recv(s, answer, sizeof(answer) - 1, 0);

		if (bytes > 0 && strcmp(answer, buffer) == 0)
			ok++;
		else
			failed++;

		closesocket(s);
		Sleep(delayMs);
	}

	cout << "ClientTy: успешных циклов " << ok << " из " << cycles
		<< ", сбоев " << failed
		<< ", время " << ((GetTickCount() - start) / 1000.0) << " с" << endl;

	WSACleanup();
	return (failed == 0) ? 0 : 1;
}
