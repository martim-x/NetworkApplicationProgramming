#define _WINSOCK_DEPRECATED_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#include <winsock2.h>
#include <windows.h>
#include <iphlpapi.h>
#include <iostream>
#include <string>
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

bool GetServer(char* call, short port, struct sockaddr* from, int* flen, const char* broadcastAddress)
{
	SOCKET sS;
	if ((sS = socket(AF_INET, SOCK_DGRAM, 0)) == INVALID_SOCKET)
		throw SetErrorMsgText("socket: ", WSAGetLastError());

	int timeout = 3000;
	if (setsockopt(sS, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout)) == SOCKET_ERROR)
	{
		int err = WSAGetLastError();
		closesocket(sS);
		throw SetErrorMsgText("setsockopt SO_RCVTIMEO: ", err);
	}

	int optval = 1;
	if (setsockopt(sS, SOL_SOCKET, SO_BROADCAST, (char*)&optval, sizeof(optval)) == SOCKET_ERROR)
	{
		int err = WSAGetLastError();
		closesocket(sS);
		throw SetErrorMsgText("setsockopt SO_BROADCAST: ", err);
	}

	SOCKADDR_IN all;
	memset(&all, 0, sizeof(all));
	all.sin_family = AF_INET;
	all.sin_port = htons(port);
	all.sin_addr.s_addr = inet_addr(broadcastAddress);

	if (sendto(sS, call, (int)strlen(call) + 1, 0, (sockaddr*)&all, sizeof(all)) == SOCKET_ERROR)
	{
		int err = WSAGetLastError();
		closesocket(sS);
		throw SetErrorMsgText("sendto: ", err);
	}

	char ibuf[64];
	memset(ibuf, 0, sizeof(ibuf));

	int libuf = recvfrom(sS, ibuf, sizeof(ibuf) - 1, 0, from, flen);
	if (libuf == SOCKET_ERROR)
	{
		int err = WSAGetLastError();
		closesocket(sS);
		if (err == WSAETIMEDOUT)
			return false;
		throw SetErrorMsgText("recvfrom: ", err);
	}

	cout << "Получен отклик: \"" << ibuf << "\"" << endl;

	if (strcmp(ibuf, call) == 0)
	{
		closesocket(sS);
		return true;
	}

	memset(from, 0, *flen);
	closesocket(sS);
	return false;
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
	SOCKADDR_IN serv;
	memset(&serv, 0, sizeof(serv));
	int lserv = sizeof(serv);

	try
	{
		if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
			throw SetErrorMsgText("Startup: ", WSAGetLastError());

		cout << "ClientB: широковещательный поиск сервера \"" << call
			<< "\" на порту " << port << " (адрес " << broadcastAddress << ")" << endl;

		if (GetServer(call, port, (struct sockaddr*)&serv, &lserv, broadcastAddress.c_str()))
		{
			cout << "Сервер найден: " << inet_ntoa(serv.sin_addr) << ":"
				<< ntohs(serv.sin_port) << endl;
		}
		else
		{
			cout << "Сервер с позывным \"" << call << "\" не найден" << endl;
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
