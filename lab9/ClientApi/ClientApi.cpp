#define _WINSOCK_DEPRECATED_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#include "ClientApi.h"
#include <iphlpapi.h>
#include <cstring>
#include <cstdlib>
#pragma comment(lib, "WS2_32.lib")
#pragma comment(lib, "iphlpapi.lib")
using namespace std;

string GetErrorMsgText(int code)
{
	switch (code)
	{
	case WSAEINTR:              return "WSAEINTR: Работа функции прервана";
	case WSAEACCES:             return "WSAEACCES: Разрешение отвергнуто";
	case WSAEFAULT:             return "WSAEFAULT: Ошибочный адрес";
	case WSAEINVAL:             return "WSAEINVAL: Ошибка в аргументе";
	case WSAEMFILE:             return "WSAEMFILE: Слишком много файлов открыто";
	case WSAEWOULDBLOCK:        return "WSAEWOULDBLOCK: Ресурс временно недоступен";
	case WSAENOTSOCK:           return "WSAENOTSOCK: Сокет задан неправильно";
	case WSAEMSGSIZE:           return "WSAEMSGSIZE: Сообщение слишком длинное";
	case WSAEADDRINUSE:         return "WSAEADDRINUSE: Адрес уже используется";
	case WSAENETDOWN:           return "WSAENETDOWN: Сеть отключена";
	case WSAENETRESET:          return "WSAENETRESET: Сеть разорвала соединение";
	case WSAECONNABORTED:       return "WSAECONNABORTED: Программный отказ связи";
	case WSAECONNRESET:         return "WSAECONNRESET: Связь восстановлена";
	case WSAENOTCONN:           return "WSAENOTCONN: Сокет не подключен";
	case WSAETIMEDOUT:          return "WSAETIMEDOUT: Закончился отведенный интервал времени";
	case WSAECONNREFUSED:       return "WSAECONNREFUSED: Соединение отклонено";
	case WSANOTINITIALISED:     return "WSANOTINITIALISED: Не выполнена инициализация WS2_32.DLL";
	default:                    return "Ошибка Winsock2, код " + to_string(code);
	}
}

void PrintError(const string& operation, int code)
{
	printf("%s: %s\n", operation.c_str(), GetErrorMsgText(code).c_str());
}

string GetLocalBroadcastAddress()
{
	ULONG size = 16384;
	PIP_ADAPTER_INFO info = (PIP_ADAPTER_INFO)new char[size];

	if (GetAdaptersInfo(info, &size) != ERROR_SUCCESS)
	{
		delete[] (char*)info;
		return "255.255.255.255";
	}

	string result = "255.255.255.255";
	PIP_ADAPTER_INFO adapter = info;

	while (adapter != NULL)
	{
		ULONG ip = inet_addr(adapter->IpAddressList.IpAddress.String);
		ULONG mask = inet_addr(adapter->IpAddressList.IpMask.String);

		if (ip != INADDR_NONE && ip != 0 && mask != INADDR_NONE && mask != 0)
		{
			ULONG broadcast = (ip & mask) | ~mask;
			result = inet_ntoa(*(in_addr*)&broadcast);
			break;
		}

		adapter = adapter->Next;
	}

	delete[] (char*)info;
	return result;
}

bool FindServer(const char* callSign, short udpPort, short tcpPort, ServerAddress& server, int timeoutMs)
{
	SOCKET s;

	if ((s = socket(AF_INET, SOCK_DGRAM, 0)) == INVALID_SOCKET)
		return false;

	int optval = 1;
	setsockopt(s, SOL_SOCKET, SO_BROADCAST, (const char*)&optval, sizeof(optval));

	u_long nonBlocking = 1;
	ioctlsocket(s, FIONBIO, &nonBlocking);

	SOCKADDR_IN target;
	memset(&target, 0, sizeof(target));
	target.sin_family = AF_INET;
	target.sin_port = htons(udpPort);
	target.sin_addr.s_addr = inet_addr(GetLocalBroadcastAddress().c_str());

	if (sendto(s, callSign, (int)strlen(callSign) + 1, 0, (sockaddr*)&target, sizeof(target)) == SOCKET_ERROR)
	{
		closesocket(s);
		return false;
	}

	DWORD start = GetTickCount();
	char buffer[64];
	bool found = false;

	while (GetTickCount() - start < (DWORD)timeoutMs)
	{
		SOCKADDR_IN from;
		int fromLength = sizeof(from);
		memset(&from, 0, sizeof(from));
		memset(buffer, 0, sizeof(buffer));

		int bytes = recvfrom(s, buffer, sizeof(buffer) - 1, 0, (sockaddr*)&from, &fromLength);

		if (bytes > 0)
		{
			buffer[bytes] = '\0';

			if (strcmp(buffer, callSign) == 0)
			{
				server.address = from;
				server.address.sin_port = htons(tcpPort);
				server.ip = inet_ntoa(from.sin_addr);
				server.port = tcpPort;
				found = true;
				break;
			}

			continue;
		}

		if (bytes == SOCKET_ERROR && WSAGetLastError() != WSAEWOULDBLOCK)
			break;

		Sleep(20);
	}

	closesocket(s);
	return found;
}

SOCKET ConnectToServer(const ServerAddress& server)
{
	SOCKET s = socket(AF_INET, SOCK_STREAM, 0);

	if (s == INVALID_SOCKET)
		return INVALID_SOCKET;

	SOCKADDR_IN target = server.address;
	target.sin_port = htons(server.port);

	if (connect(s, (sockaddr*)&target, sizeof(target)) == SOCKET_ERROR)
	{
		int err = WSAGetLastError();
		closesocket(s);
		WSASetLastError(err);
		return INVALID_SOCKET;
	}

	DWORD timeout = 5000;
	setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));

	return s;
}

bool SendText(SOCKET s, const char* text)
{
	int bytes = (int)strlen(text) + 1;

	if (send(s, text, bytes, 0) == SOCKET_ERROR)
		return false;

	return true;
}

int ReceiveText(SOCKET s, char* buffer, int size)
{
	memset(buffer, 0, size);
	int bytes = recv(s, buffer, size - 1, 0);

	if (bytes <= 0)
		return bytes;

	buffer[bytes] = '\0';
	return bytes;
}

bool SendRequest(SOCKET s, const char* code)
{
	return SendText(s, code);
}

bool RequestEcho(SOCKET s, const char* text, char* answer, int size)
{
	if (!SendText(s, text))
		return false;

	int bytes = ReceiveText(s, answer, size);
	return bytes > 0;
}

bool RequestTime(SOCKET s, char* answer, int size)
{
	if (!SendText(s, "time"))
		return false;

	int bytes = ReceiveText(s, answer, size);
	return bytes > 0;
}

bool RequestRand(SOCKET s, int& value)
{
	if (!SendText(s, "rand"))
		return false;

	char buffer[sizeof(int)];
	int received = 0;

	while (received < (int)sizeof(int))
	{
		int bytes = recv(s, buffer + received, (int)sizeof(int) - received, 0);

		if (bytes <= 0)
			return false;

		received += bytes;
	}

	memcpy(&value, buffer, sizeof(int));
	return true;
}

bool SendInvalidRequest(SOCKET s, char* answer, int size)
{
	if (!SendText(s, "wrong-request"))
		return false;

	int bytes = ReceiveText(s, answer, size);
	return bytes > 0;
}

double ElapsedSeconds(DWORD startTick)
{
	return (GetTickCount() - startTick) / 1000.0;
}
