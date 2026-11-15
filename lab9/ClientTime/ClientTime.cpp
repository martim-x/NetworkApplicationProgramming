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

// Режимы запуска:
//   ClientTime.exe [позывной] [UDP-порт] [TCP-порт] [запросов]  — широковещательный поиск (по умолчанию)
//   ClientTime.exe --dns <hostname> <tcpPort> [count]           — поиск по имени хоста
// Некорректные аргументы и неполный сеанс — ненулевой код возврата.

static int ParsePortArg(const char* text, const char* title, short& port)
{
	int value = 0;

	if (!ParseIntStrict(text, value) || value < 1 || value > 65535)
	{
		fprintf(stderr, "ClientTime: некорректный %s \"%s\" (нужно целое 1..65535)\n", title, text);
		return 2;
	}

	port = (short)value;
	return 0;
}

int main(int argc, char* argv[])
{
	SetConsoleOutputCP(CP_UTF8);

	bool dnsMode = (argc > 1 && strcmp(argv[1], "--dns") == 0);
	const char* callSign = "ConcurrentServer";
	const char* hostname = "";
	short udpPort = 9200;
	short tcpPort = 2000;
	int requestTotal = 3;

	if (dnsMode)
	{
		if (argc < 4)
		{
			fprintf(stderr, "Использование: ClientTime.exe --dns <hostname> <tcpPort> [count]\n");
			return 2;
		}

		hostname = argv[2];

		int rc = ParsePortArg(argv[3], "TCP-порт", tcpPort);
		if (rc != 0)
			return rc;

		if (argc > 4)
		{
			int countValue = 0;

			if (!ParseIntStrict(argv[4], countValue) || countValue < 1)
			{
				fprintf(stderr, "ClientTime: некорректное количество запросов \"%s\" (нужно целое >= 1)\n", argv[4]);
				return 2;
			}

			requestTotal = countValue;
		}
	}
	else
	{
		if (argc > 1)
			callSign = argv[1];

		int rc = 0;

		if (argc > 2 && (rc = ParsePortArg(argv[2], "UDP-порт", udpPort)) != 0)
			return rc;

		if (argc > 3 && (rc = ParsePortArg(argv[3], "TCP-порт", tcpPort)) != 0)
			return rc;

		if (argc > 4)
		{
			int countValue = 0;

			if (!ParseIntStrict(argv[4], countValue) || countValue < 1)
			{
				fprintf(stderr, "ClientTime: некорректное количество запросов \"%s\" (нужно целое >= 1)\n", argv[4]);
				return 2;
			}

			requestTotal = countValue;
		}
	}

	WSADATA wsaData;
	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
	{
		PrintError("Startup", WSAGetLastError());
		return 1;
	}

	ServerAddress server;
	bool found = false;

	if (dnsMode)
	{
		cout << "ClientTime: подключение по имени \"" << hostname << "\" (TCP-порт " << tcpPort << ")" << endl;
		found = FindServerByName(hostname, tcpPort, server, 3000);

		if (!found)
		{
			cout << "ClientTime: не удалось разрешить имя \"" << hostname
				<< "\" или подключиться к серверу" << endl;
			PrintError("FindServerByName", WSAGetLastError());
			WSACleanup();
			return 1;
		}
	}
	else
	{
		cout << "ClientTime: поиск сервера \"" << callSign << "\" (широковещание, UDP-порт "
			<< udpPort << ", TCP-порт " << tcpPort << ")" << endl;
		found = FindServer(callSign, udpPort, tcpPort, server, 3000);

		if (!found)
		{
			cout << "ClientTime: сервер не найден" << endl;
			WSACleanup();
			return 1;
		}
	}

	cout << "ClientTime: найден сервер " << server.ip << ":" << (unsigned short)server.port << endl;

	SOCKET s = ConnectToServer(server);
	if (s == INVALID_SOCKET)
	{
		PrintError("ConnectToServer", WSAGetLastError());
		WSACleanup();
		return 1;
	}

	char answer[128];
	int receivedOk = 0;
	DWORD start = GetTickCount();

	for (int i = 1; i <= requestTotal; i++)
	{
		if (RequestTime(s, answer, sizeof(answer)))
		{
			receivedOk++;
			cout << "  ответ " << i << ": " << answer << endl;
		}
		else
		{
			cout << "  ошибка при получении времени " << i << endl;
		}
	}

	if (receivedOk < requestTotal)
	{
		closesocket(s);
		cout << "ClientTime: корректных ответов " << receivedOk << " из " << requestTotal
			<< ", время сеанса " << ElapsedSeconds(start) << " с" << endl;
		cout << "ClientTime: получено меньше ответов, чем запрошено — сеанс завершился с ошибкой" << endl;
		WSACleanup();
		return 1;
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
