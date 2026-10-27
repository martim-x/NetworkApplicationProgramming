#define _WINSOCK_DEPRECATED_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#include <winsock2.h>
#include <windows.h>
#include <iphlpapi.h>
#include <iostream>
#include <string>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <set>
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
	case WSAEDESTADDRREQ:       return "WSAEDESTADDRREQ: Требуется адрес расположения";
	case WSAEMSGSIZE:           return "WSAEMSGSIZE: Сообщение слишком длинное";
	case WSAEPROTOTYPE:         return "WSAEPROTOTYPE: Неправильный тип протокола для сокета";
	case WSAEOPNOTSUPP:         return "WSAEOPNOTSUPP: Операция не поддерживается";
	case WSAEADDRINUSE:         return "WSAEADDRINUSE: Адрес уже используется";
	case WSAEADDRNOTAVAIL:      return "WSAEADDRNOTAVAIL: Запрошенный адрес не может быть использован";
	case WSAENETDOWN:           return "WSAENETDOWN: Сеть отключена";
	case WSAENETUNREACH:        return "WSAENETUNREACH: Сеть не достижима";
	case WSAENETRESET:          return "WSAENETRESET: Сеть разорвала соединение";
	case WSAECONNABORTED:       return "WSAECONNABORTED: Программный отказ связи";
	case WSAECONNRESET:         return "WSAECONNRESET: Связь восстановлена";
	case WSAENOBUFS:            return "WSAENOBUFS: Не хватает памяти для буферов";
	case WSAEISCONN:            return "WSAEISCONN: Сокет уже подключен";
	case WSAENOTCONN:           return "WSAENOTCONN: Сокет не подключен";
	case WSAETIMEDOUT:          return "WSAETIMEDOUT: Закончился отведенный интервал времени";
	case WSAECONNREFUSED:       return "WSAECONNREFUSED: Соединение отклонено";
	case WSAEHOSTDOWN:          return "WSAEHOSTDOWN: Хост в неработоспособном состоянии";
	case WSAEHOSTUNREACH:       return "WSAEHOSTUNREACH: Нет маршрута для хоста";
	case WSASYSNOTREADY:        return "WSASYSNOTREADY: Сеть не доступна";
	case WSAVERNOTSUPPORTED:    return "WSAVERNOTSUPPORTED: Данная версия недоступна";
	case WSANOTINITIALISED:     return "WSANOTINITIALISED: Не выполнена инициализация WS2_32.DLL";
	case WSASYSCALLFAILURE:     return "WSASYSCALLFAILURE: Аварийное завершение системного вызова";
	default:                    return "Ошибка Winsock2, код " + to_string(code);
	}
}

string SetErrorMsgText(string msgText, int code)
{
	return msgText + GetErrorMsgText(code);
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

string GetOwnHostName()
{
	char hostName[256];
	if (gethostname(hostName, sizeof(hostName)) == SOCKET_ERROR)
		return "не определено (" + GetErrorMsgText(WSAGetLastError()) + ")";

	return hostName;
}

string GetHostNameByAddress(const char* address)
{
	ULONG ip = inet_addr(address);
	struct hostent* host = gethostbyaddr((const char*)&ip, sizeof(ip), AF_INET);

	if (host == NULL)
		return "не разрешается (" + GetErrorMsgText(WSAGetLastError()) + ")";

	return host->h_name;
}

bool GetRequestFromClient(char* name, short port, struct sockaddr* from, int* flen)
{
	SOCKET sS;
	if ((sS = socket(AF_INET, SOCK_DGRAM, 0)) == INVALID_SOCKET)
		throw SetErrorMsgText("socket: ", WSAGetLastError());

	int timeout = 3000;
	if (setsockopt(sS, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout)) == SOCKET_ERROR)
		throw SetErrorMsgText("setsockopt: ", WSAGetLastError());

	int reuse = 1;
	if (setsockopt(sS, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse)) == SOCKET_ERROR)
		throw SetErrorMsgText("setsockopt SO_REUSEADDR: ", WSAGetLastError());

	SOCKADDR_IN serv;
	memset(&serv, 0, sizeof(serv));
	serv.sin_family = AF_INET;
	serv.sin_port = htons(port);
	serv.sin_addr.s_addr = INADDR_ANY;

	if (bind(sS, (LPSOCKADDR)&serv, sizeof(serv)) == SOCKET_ERROR)
		throw SetErrorMsgText("bind: ", WSAGetLastError());

	char ibuf[64];

	try
	{
		while (true)
		{
			memset(ibuf, 0, sizeof(ibuf));
			int libuf = recvfrom(sS, ibuf, sizeof(ibuf) - 1, 0, from, flen);

			if (libuf == SOCKET_ERROR)
			{
				int err = WSAGetLastError();
				if (err == WSAETIMEDOUT)
				{
					closesocket(sS);
					return false;
				}
				throw SetErrorMsgText("recvfrom: ", err);
			}

			if (strcmp(ibuf, name) == 0)
			{
				closesocket(sS);
				return true;
			}

			cout << "  отклонён чужой запрос: \"" << ibuf << "\"" << endl;
		}
	}
	catch (string)
	{
		closesocket(sS);
		throw;
	}
}

bool PutAnswerToClient(char* name, struct sockaddr* to, int* lto)
{
	SOCKET sS;
	if ((sS = socket(AF_INET, SOCK_DGRAM, 0)) == INVALID_SOCKET)
		throw SetErrorMsgText("socket: ", WSAGetLastError());

	int libuf = sendto(sS, name, (int)strlen(name) + 1, 0, to, *lto);
	if (libuf == SOCKET_ERROR)
	{
		int err = WSAGetLastError();
		closesocket(sS);
		throw SetErrorMsgText("sendto: ", err);
	}

	closesocket(sS);
	return true;
}

int CountServers(char* call, short port, const char* broadcastAddress)
{
	SOCKET sS;
	if ((sS = socket(AF_INET, SOCK_DGRAM, 0)) == INVALID_SOCKET)
		throw SetErrorMsgText("socket: ", WSAGetLastError());

	int timeout = 700;
	if (setsockopt(sS, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout)) == SOCKET_ERROR)
		throw SetErrorMsgText("setsockopt: ", WSAGetLastError());

	int optval = 1;
	if (setsockopt(sS, SOL_SOCKET, SO_BROADCAST, (char*)&optval, sizeof(optval)) == SOCKET_ERROR)
		throw SetErrorMsgText("setsockopt SO_BROADCAST: ", WSAGetLastError());

	SOCKADDR_IN all;
	memset(&all, 0, sizeof(all));
	all.sin_family = AF_INET;
	all.sin_port = htons(port);
	all.sin_addr.s_addr = inet_addr(broadcastAddress);

	if (sendto(sS, call, (int)strlen(call) + 1, 0, (sockaddr*)&all, sizeof(all)) == SOCKET_ERROR)
		throw SetErrorMsgText("sendto: ", WSAGetLastError());

	set<string> addresses;
	char ibuf[64];
	SOCKADDR_IN from;
	int lfrom = sizeof(from);

	while (true)
	{
		memset(ibuf, 0, sizeof(ibuf));
		int libuf = recvfrom(sS, ibuf, sizeof(ibuf) - 1, 0, (sockaddr*)&from, &lfrom);

		if (libuf == SOCKET_ERROR)
		{
			int err = WSAGetLastError();
			if (err == WSAETIMEDOUT)
				break;
			closesocket(sS);
			throw SetErrorMsgText("recvfrom: ", err);
		}

		if (strcmp(ibuf, call) == 0)
		{
			string ip = inet_ntoa(from.sin_addr);
			addresses.insert(ip);
			cout << "  отклик от " << ip << ":" << ntohs(from.sin_port) << endl;
		}
	}

	closesocket(sS);
	return (int)addresses.size();
}

int main(int argc, char* argv[])
{
	SetConsoleOutputCP(CP_UTF8);

	char call[32];
	strncpy(call, (argc > 1) ? argv[1] : "Hello", sizeof(call) - 1);
	call[sizeof(call) - 1] = '\0';

	short port = (short)((argc > 2) ? atoi(argv[2]) : 2000);
	string broadcastAddress = (argc > 3) ? argv[3] : GetLocalBroadcastAddress();

	WSADATA wsaData;

	try
	{
		if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
			throw SetErrorMsgText("Startup: ", WSAGetLastError());

		cout << "ServerB: позывной \"" << call << "\", UDP-порт " << port << endl;
		cout << "ServerB: символическое имя компьютера: " << GetOwnHostName() << endl;

		int servers = CountServers(call, port, broadcastAddress.c_str());
		if (servers > 0)
			cout << "ВНИМАНИЕ: в сети обнаружены серверы с позывным \"" << call
				<< "\": " << servers << " (запрос отправлен до привязки своего сокета)" << endl;
		else
			cout << "В сети других серверов с этим позывным не найдено" << endl;

		cout << "ServerB: ожидание запросов клиентов..." << endl;

		while (true)
		{
			SOCKADDR_IN clnt;
			memset(&clnt, 0, sizeof(clnt));
			int lclnt = sizeof(clnt);

			if (!GetRequestFromClient(call, port, (struct sockaddr*)&clnt, &lclnt))
			{
				cout << "ServerB: запросов нет (таймаут ожидания)" << endl;
				continue;
			}

			string clientIp = inet_ntoa(clnt.sin_addr);
			cout << "Запрос от клиента " << clientIp << ":" << ntohs(clnt.sin_port)
				<< " (имя: " << GetHostNameByAddress(clientIp.c_str()) << ")" << endl;

			PutAnswerToClient(call, (struct sockaddr*)&clnt, &lclnt);
			cout << "Клиенту отправлен позывной \"" << call << "\"" << endl;
		}

		if (WSACleanup() == SOCKET_ERROR)
			throw SetErrorMsgText("Cleanup: ", WSAGetLastError());
	}
	catch (string errorMsgText)
	{
		cout << endl << "Ошибка: " << errorMsgText << endl;
		return 1;
	}

	return 0;
}
