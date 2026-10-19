#define _WINSOCK_DEPRECATED_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#include <string>
#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include "Winsock2.h"
#include <windows.h> #pragma comment(lib, "WS2_32.lib")
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
	default:                return "Ошибка Winsock, код " + to_string(code);
	}
}

string SetErrorMsgText(string msgText, int code)
{
	return msgText + GetErrorMsgText(code);
}

int main(int argc, char* argv[])
{
	const char* serverIp = (argc > 1) ? argv[1] : "127.0.0.1";
	int serverPort = (argc > 2) ? atoi(argv[2]) : 2000;
	int messageTotal = (argc > 3) ? atoi(argv[3]) : 5000;

	SOCKET cC;
	WSADATA wsaData;
	char sendBuffer[50];
	clock_t startTime, endTime;

	try
	{
		if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
			throw SetErrorMsgText("Startup:", WSAGetLastError());

				if ((cC = socket(AF_INET, SOCK_DGRAM, 0)) == INVALID_SOCKET)
			throw SetErrorMsgText("socket:", WSAGetLastError());

		SOCKADDR_IN serverAddress;
		serverAddress.sin_family = AF_INET;
		serverAddress.sin_port = htons(serverPort);
		serverAddress.sin_addr.s_addr = inet_addr(serverIp);

		cout << "ClientUFlood: sending " << messageTotal << " datagrams to "
			<< serverIp << ":" << serverPort << " (fire-and-forget, echoes are not read)..." << endl;

						startTime = clock();
		for (int i = 1; i <= messageTotal; i++)
		{
			sprintf(sendBuffer, "Flood %05d", i);
			if (sendto(cC, sendBuffer, (int)(strlen(sendBuffer) + 1), 0,
				(sockaddr*)&serverAddress, sizeof(serverAddress)) == SOCKET_ERROR)
				throw SetErrorMsgText("sendto:", WSAGetLastError());
		}
		endTime = clock();

		double seconds = ((double)(endTime - startTime)) / CLOCKS_PER_SEC;
		cout << "Sent " << messageTotal << " datagrams in " << seconds << " seconds"
			<< " (" << (int)(messageTotal / (seconds > 0.001 ? seconds : 0.001)) << " per second)." << endl;

								cout << "Waiting 15 seconds for the server to drain its queue..." << endl;
		Sleep(15000);

		if (closesocket(cC) == SOCKET_ERROR)
			throw SetErrorMsgText("closesocket:", WSAGetLastError());
		if (WSACleanup() == SOCKET_ERROR)
			throw SetErrorMsgText("Cleanup:", WSAGetLastError());
	}
	catch (string errorMsgText)
	{
		cout << endl << "WSAGetLastError: " << errorMsgText;
	}

	return 0;
}
