#define _WINSOCK_DEPRECATED_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#include "ClientApi.h"
#include <iphlpapi.h>
#include <ws2tcpip.h>
#include <cstring>
#include <cstdlib>
#include <map>
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

// Поиск сервера по имени хоста: getaddrinfo, без пробного TCP-соединения
// (каждое такое соединение сервер засчитывал бы в статистику как отказ).
// Все результаты адресов освобождаются.
bool FindServerByName(const char* hostname, short tcpPort, ServerAddress& server, int timeoutMs)
{
	// Preserve the unsigned bit pattern for valid ports 32768..65535.
	unsigned short portValue = (unsigned short)tcpPort;
	if (hostname == NULL || hostname[0] == '\0' || portValue == 0 || timeoutMs <= 0)
	{
		WSASetLastError(WSAEINVAL);
		return false;
	}

	char portText[16];
	sprintf(portText, "%u", (unsigned)portValue);

	addrinfo hints;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;

	addrinfo* results = NULL;
	int gaiResult = getaddrinfo(hostname, portText, &hints, &results);

	if (gaiResult != 0 || results == NULL)
	{
		WSASetLastError(gaiResult);
		return false;
	}

	DWORD deadline = GetTickCount() + (DWORD)timeoutMs;
	bool found = false;
	(void)deadline;

	// Пробное TCP-соединение не открываем: каждое соединение сервер засчитал бы
	// как подключение/отказ и искажал бы статистику. Имя и адрес полностью
	// разрешены DNS; доступность порта проверит ConnectToServer при первом
	// подключении. Берём первый IPv4-адрес результата.
	for (addrinfo* ai = results; ai != NULL && !found; ai = ai->ai_next)
	{
		if (ai->ai_family != AF_INET)
			continue;

		SOCKADDR_IN* sin = (SOCKADDR_IN*)ai->ai_addr;
		server.address = *sin;
		server.address.sin_port = htons((u_short)portValue);
		server.ip = inet_ntoa(sin->sin_addr);
		server.port = tcpPort;
		found = true;
	}

	freeaddrinfo(results);

	if (!found)
		WSASetLastError(WSANO_DATA);

	return found;
}

static void ForgetSocketState(SOCKET s);

SOCKET ConnectToServer(const ServerAddress& server)
{
	SOCKET s = socket(AF_INET, SOCK_STREAM, 0);

	if (s == INVALID_SOCKET)
		return INVALID_SOCKET;

	SOCKADDR_IN target = server.address;
	target.sin_port = htons(server.port);

	// Неблокирующее подключение с ограничением по времени: connect к
	// недоступному хосту иначе висит до ~21 с.
	u_long nonBlocking = 1;
	ioctlsocket(s, FIONBIO, &nonBlocking);

	if (connect(s, (sockaddr*)&target, sizeof(target)) == SOCKET_ERROR &&
		WSAGetLastError() != WSAEWOULDBLOCK)
	{
		int err = WSAGetLastError();
		closesocket(s);
		WSASetLastError(err);
		return INVALID_SOCKET;
	}

	fd_set writeSet;
	FD_ZERO(&writeSet);
	FD_SET(s, &writeSet);

	timeval wait;
	wait.tv_sec = 3;
	wait.tv_usec = 0;

	if (select(0, NULL, &writeSet, NULL, &wait) <= 0)
	{
		closesocket(s);
		WSASetLastError(WSAETIMEDOUT);
		return INVALID_SOCKET;
	}

	int soError = 0;
	int optLen = sizeof(soError);

	if (getsockopt(s, SOL_SOCKET, SO_ERROR, (char*)&soError, &optLen) != 0 || soError != 0)
	{
		int err = (soError != 0) ? soError : WSAGetLastError();
		closesocket(s);
		WSASetLastError(err);
		return INVALID_SOCKET;
	}

	u_long blocking = 0;
	ioctlsocket(s, FIONBIO, &blocking);

	DWORD timeout = 5000;
	setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));

	// SOCKET-значения могут переиспользоваться: сбросить возможно устаревший буфер приёма
	ForgetSocketState(s);

	return s;
}

bool SendAllBytes(SOCKET s, const char* data, int bytes)
{
	if (data == NULL || bytes < 0)
		return false;

	int sent = 0;

	while (sent < bytes)
	{
		int chunk = send(s, data + sent, bytes - sent, 0);

		if (chunk == SOCKET_ERROR || chunk == 0)
			return false;

		sent += chunk;
	}

	return true;
}

bool SendText(SOCKET s, const char* text)
{
	if (text == NULL)
		return false;

	return SendAllBytes(s, text, (int)strlen(text) + 1);
}

// Приём текста точными NUL-завершёнными фреймами с буфером на каждый сокет:
// частичные recv склеиваются, несколько фреймов в одном пакете читаются по одному.
static const size_t kMaxFrameBytes = 1024 * 1024;

static map<SOCKET, string>& SocketInbox()
{
	static map<SOCKET, string> inbox;
	return inbox;
}

static void ForgetSocketState(SOCKET s)
{
	SocketInbox().erase(s);
}

int ReceiveText(SOCKET s, char* buffer, int size)
{
	if (buffer == NULL || size <= 0)
		return SOCKET_ERROR;

	string& inbox = SocketInbox()[s];

	while (true)
	{
		size_t nul = inbox.find('\0');

		if (nul != string::npos)
		{
			if (nul >= (size_t)size)
			{
				// фрейм не помещается в буфер вызывающего — пропустить его как испорченный
				inbox.erase(0, nul + 1);
				WSASetLastError(WSAEMSGSIZE);
				return SOCKET_ERROR;
			}

			memcpy(buffer, inbox.data(), nul);
			buffer[nul] = '\0';
			inbox.erase(0, nul + 1);

			// пустой хвост записи не держать: SOCKET-значения переиспользуются
			if (inbox.empty())
				ForgetSocketState(s);

			return (int)nul;
		}

		if (inbox.size() > kMaxFrameBytes)
		{
			// ответ без завершающего NUL — защита от зависания на мусорном потоке
			ForgetSocketState(s);
			WSASetLastError(WSAEMSGSIZE);
			return SOCKET_ERROR;
		}

		char chunk[8192];
		int bytes = recv(s, chunk, (int)sizeof(chunk), 0);

		if (bytes > 0)
		{
			inbox.append(chunk, bytes);
			continue;
		}

		// EOF (0) или ошибка/таймаут приёма (-1): фрейм не завершён — это отказ
		int err = (bytes == 0) ? WSAECONNRESET : WSAGetLastError();
		ForgetSocketState(s);
		WSASetLastError(err);
		return SOCKET_ERROR;
	}
}

// Строгий разбор целого числа без atoi-тихих ошибок ("12abc" -> 12 и т.п.)
bool ParseIntStrict(const char* text, int& value)
{
	value = 0;

	if (text == NULL || text[0] == '\0')
		return false;

	bool negative = false;
	const char* p = text;

	if (*p == '-')
	{
		negative = true;
		p++;
	}
	else if (*p == '+')
	{
		p++;
	}

	if (*p == '\0')
		return false;

	long long acc = 0;

	for (; *p != '\0'; p++)
	{
		if (*p < '0' || *p > '9')
			return false;

		acc = acc * 10 + (*p - '0');

		if (acc > 1000000000LL)
			return false;
	}

	value = (int)(negative ? -acc : acc);
	return true;
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
