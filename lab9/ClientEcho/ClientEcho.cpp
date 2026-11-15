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
//   ClientEcho.exe [позывной] [UDP-порт] [TCP-порт] [сообщений] [задержка мс]  — широковещательный поиск (по умолчанию)
//   ClientEcho.exe --dns <hostname> <tcpPort> [count] [delayMs]               — поиск по имени хоста
// Некорректные аргументы и неполный сеанс — ненулевой код возврата.

static int ParsePortArg(const char* text, const char* title, short& port)
{
	int value = 0;

	if (!ParseIntStrict(text, value) || value < 1 || value > 65535)
	{
		fprintf(stderr, "ClientEcho: некорректный %s \"%s\" (нужно целое 1..65535)\n", title, text);
		return 2;
	}

	port = (short)value;
	return 0;
}

static int ParseCountArg(const char* text, const char* title, int& count)
{
	if (!ParseIntStrict(text, count) || count < 1)
	{
		fprintf(stderr, "ClientEcho: некорректное количество (%s) \"%s\" (нужно целое >= 1)\n", title, text);
		return 2;
	}

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
	int messageTotal = 5;
	int delayMs = 100;

	if (dnsMode)
	{
		if (argc < 4)
		{
			fprintf(stderr, "Использование: ClientEcho.exe --dns <hostname> <tcpPort> [count] [delayMs]\n");
			return 2;
		}

		hostname = argv[2];

		int rc = ParsePortArg(argv[3], "TCP-порт", tcpPort);
		if (rc != 0)
			return rc;

		if (argc > 4 && (rc = ParseCountArg(argv[4], "count", messageTotal)) != 0)
			return rc;

		if (argc > 5)
		{
			int delayValue = 0;

			if (!ParseIntStrict(argv[5], delayValue) || delayValue < 0)
			{
				fprintf(stderr, "ClientEcho: некорректная задержка \"%s\" (нужно целое >= 0)\n", argv[5]);
				return 2;
			}

			delayMs = delayValue;
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

		if (argc > 4 && (rc = ParseCountArg(argv[4], "сообщений", messageTotal)) != 0)
			return rc;

		if (argc > 5)
		{
			int delayValue = 0;

			if (!ParseIntStrict(argv[5], delayValue) || delayValue < 0)
			{
				fprintf(stderr, "ClientEcho: некорректная задержка \"%s\" (нужно целое >= 0)\n", argv[5]);
				return 2;
			}

			delayMs = delayValue;
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
		cout << "ClientEcho: подключение по имени \"" << hostname << "\" (TCP-порт " << tcpPort << ")" << endl;
		found = FindServerByName(hostname, tcpPort, server, 3000);

		if (!found)
		{
			cout << "ClientEcho: не удалось разрешить имя \"" << hostname
				<< "\" или подключиться к серверу" << endl;
			PrintError("FindServerByName", WSAGetLastError());
			WSACleanup();
			return 1;
		}
	}
	else
	{
		cout << "ClientEcho: поиск сервера \"" << callSign << "\" (широковещание, UDP-порт "
			<< udpPort << ", TCP-порт " << tcpPort << ")" << endl;
		found = FindServer(callSign, udpPort, tcpPort, server, 3000);

		if (!found)
		{
			cout << "ClientEcho: сервер не найден" << endl;
			WSACleanup();
			return 1;
		}
	}

	cout << "ClientEcho: найден сервер " << server.ip << ":" << (unsigned short)server.port << endl;

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

	closesocket(s);

	if (receivedOk < messageTotal)
	{
		cout << "ClientEcho: корректных ответов " << receivedOk << " из " << messageTotal
			<< ", время сеанса " << ElapsedSeconds(start) << " с" << endl;
		cout << "ClientEcho: получено меньше ответов, чем запрошено — сеанс завершился с ошибкой" << endl;
		WSACleanup();
		return 1;
	}

	cout << "ClientEcho: корректных ответов " << receivedOk << " из " << messageTotal
		<< ", время сеанса " << ElapsedSeconds(start) << " с" << endl;

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
