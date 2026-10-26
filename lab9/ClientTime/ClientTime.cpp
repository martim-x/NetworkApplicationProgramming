#define _WINSOCK_DEPRECATED_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#include "../ClientApi/ClientApi.h"
#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
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

	cout << "ClientTime: поиск сервера \"" << callSign << "\" (широковещание, UDP-порт "
		<< udpPort << ", TCP-порт " << tcpPort << ")" << endl;

	ServerAddress server;
	if (!FindServer(callSign, udpPort, tcpPort, server, 3000))
	{
		cout << "ClientTime: сервер не найден" << endl;
		WSACleanup();
		return 1;
	}

	cout << "ClientTime: найден сервер " << server.ip << ":" << server.port << endl;

	SOCKET s = ConnectToServer(server);
	if (s == INVALID_SOCKET)
	{
		PrintError("ConnectToServer", WSAGetLastError());
		WSACleanup();
		return 1;
	}

	if (!SendRequest(s, "time"))
	{
		PrintError("SendRequest", WSAGetLastError());
		closesocket(s);
		WSACleanup();
		return 1;
	}

	char answer[128];
	int receivedOk = 0;
	DWORD start = GetTickCount();

	for (int i = 1; i <= requestTotal; i++)
	{
		if (i > 1)
		{
			if (!SendRequest(s, "time"))
				break;
		}

		if (ReceiveText(s, answer, sizeof(answer)) > 0)
		{
			receivedOk++;
			cout << "  ответ " << i << ": " << answer << endl;
		}
	}

	cout << "ClientTime: корректных ответов " << receivedOk << " из " << requestTotal
		<< ", время сеанса " << ElapsedSeconds(start) << " с" << endl;

	if (SendRequest(s, "stop-time"))
		cout << "ClientTime: отправлено сообщение, отличное от time — сеанс должен завершиться" << endl;

	closesocket(s);

	SOCKET bad = ConnectToServer(server);
	if (bad != INVALID_SOCKET)
	{
		char errorAnswer[128];
		if (SendInvalidRequest(bad, errorAnswer, sizeof(errorAnswer)))
			cout << "ClientTime: ответ на некорректный запрос: " << errorAnswer << endl;

		closesocket(bad);
	}

	WSACleanup();
	return 0;
}
