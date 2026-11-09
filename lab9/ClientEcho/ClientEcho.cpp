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
	int messageTotal = (argc > 4) ? atoi(argv[4]) : 5;
	int delayMs = (argc > 5) ? atoi(argv[5]) : 100;

	WSADATA wsaData;
	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
	{
		PrintError("Startup", WSAGetLastError());
		return 1;
	}

	cout << "ClientEcho: поиск сервера \"" << callSign << "\" (широковещание, UDP-порт "
		<< udpPort << ", TCP-порт " << tcpPort << ")" << endl;

	ServerAddress server;
	if (!FindServer(callSign, udpPort, tcpPort, server, 3000))
	{
		cout << "ClientEcho: сервер не найден" << endl;
		WSACleanup();
		return 1;
	}

	cout << "ClientEcho: найден сервер " << server.ip << ":" << server.port << endl;

	SOCKET s = ConnectToServer(server);
	if (s == INVALID_SOCKET)
	{
		PrintError("ConnectToServer", WSAGetLastError());
		WSACleanup();
		return 1;
	}

	if (!SendRequest(s, "echo"))
	{
		PrintError("SendRequest", WSAGetLastError());
		closesocket(s);
		WSACleanup();
		return 1;
	}

	char answer[1024];
	int receivedOk = 0;
	DWORD start = GetTickCount();

	for (int i = 1; i <= messageTotal; i++)
	{
		char message[256];
		sprintf_s(message, "Hello from ClientEcho %03d", i);

		if (RequestEcho(s, message, answer, sizeof(answer)) && strcmp(answer, message) == 0)
		{
			receivedOk++;
			cout << "  эхо: " << answer << endl;
		}
		else
		{
			cout << "  ошибка при обмене сообщением " << i << endl;
		}

		if (delayMs > 0)
			Sleep(delayMs);
	}

	cout << "ClientEcho: корректных ответов " << receivedOk << " из " << messageTotal
		<< ", время сеанса " << ElapsedSeconds(start) << " с" << endl;

	closesocket(s);

	SOCKET bad = ConnectToServer(server);
	if (bad != INVALID_SOCKET)
	{
		char errorAnswer[128];
		if (SendInvalidRequest(bad, errorAnswer, sizeof(errorAnswer)))
			cout << "ClientEcho: ответ на некорректный запрос: " << errorAnswer << endl;
		else
			cout << "ClientEcho: сервер не ответил на некорректный запрос" << endl;

		closesocket(bad);
	}

	WSACleanup();
	return 0;
}
