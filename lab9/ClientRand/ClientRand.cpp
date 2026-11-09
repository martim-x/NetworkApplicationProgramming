#define _WINSOCK_DEPRECATED_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#include "../ClientApi/ClientApi.h"
#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#pragma comment(lib, "WS2_32.lib")
using namespace std;

int main(int argc, char* argv[])
{
	SetConsoleOutputCP(CP_UTF8);

	const char* callSign = (argc > 1) ? argv[1] : "ConcurrentServer";
	short udpPort = (short)((argc > 2) ? atoi(argv[2]) : 9200);
	short tcpPort = (short)((argc > 3) ? atoi(argv[3]) : 2000);
	int requestTotal = (argc > 4) ? atoi(argv[4]) : 3;

	WSADATA wsaData;
	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
	{
		PrintError("Startup", WSAGetLastError());
		return 1;
	}

	cout << "ClientRand: поиск сервера \"" << callSign << "\" (широковещание, UDP-порт "
		<< udpPort << ", TCP-порт " << tcpPort << ")" << endl;

	ServerAddress server;
	if (!FindServer(callSign, udpPort, tcpPort, server, 3000))
	{
		cout << "ClientRand: сервер не найден" << endl;
		WSACleanup();
		return 1;
	}

	cout << "ClientRand: найден сервер " << server.ip << ":" << server.port << endl;

	SOCKET s = ConnectToServer(server);
	if (s == INVALID_SOCKET)
	{
		PrintError("ConnectToServer", WSAGetLastError());
		WSACleanup();
		return 1;
	}

	if (!SendRequest(s, "rand"))
	{
		PrintError("SendRequest", WSAGetLastError());
		closesocket(s);
		WSACleanup();
		return 1;
	}

	int receivedOk = 0;
	int value = 0;
	DWORD start = GetTickCount();

	for (int i = 1; i <= requestTotal; i++)
	{
		if (i == 1)
		{
			char buffer[sizeof(int)];
			int received = 0;

			while (received < (int)sizeof(int))
			{
				int bytes = recv(s, buffer + received, (int)sizeof(int) - received, 0);
				if (bytes <= 0)
					break;
				received += bytes;
			}

			if (received == (int)sizeof(int))
			{
				memcpy(&value, buffer, sizeof(int));
				receivedOk++;
				cout << "  случайное число " << i << ": " << value << endl;
			}
		}
		else if (RequestRand(s, value))
		{
			receivedOk++;
			cout << "  случайное число " << i << ": " << value << endl;
		}
		else
		{
			cout << "  ошибка при получении числа " << i << endl;
		}
	}

	cout << "ClientRand: корректных ответов " << receivedOk << " из " << requestTotal
		<< ", время сеанса " << ElapsedSeconds(start) << " с" << endl;

	if (SendRequest(s, "stop-rand"))
		cout << "ClientRand: отправлено сообщение, отличное от rand — сеанс должен завершиться" << endl;

	closesocket(s);

	SOCKET bad = ConnectToServer(server);
	if (bad != INVALID_SOCKET)
	{
		char errorAnswer[128];
		if (SendInvalidRequest(bad, errorAnswer, sizeof(errorAnswer)))
			cout << "ClientRand: ответ на некорректный запрос: " << errorAnswer << endl;

		closesocket(bad);
	}

	WSACleanup();
	return 0;
}
