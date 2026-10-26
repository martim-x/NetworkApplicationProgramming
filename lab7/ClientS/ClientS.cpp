#define _WINSOCK_DEPRECATED_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#include <winsock2.h>
#include <windows.h>
#include <iostream>
#include <string>
#include <cstring>
#include <cstdlib>
#pragma comment(lib, "WS2_32.lib")
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
	case WSAEADDRINUSE:         return "WSAEADDRINUSE: Адрес уже используется";
	case WSAENETDOWN:           return "WSAENETDOWN: Сеть отключена";
	case WSAENETUNREACH:        return "WSAENETUNREACH: Сеть не достижима";
	case WSAENETRESET:          return "WSAENETRESET: Сеть разорвала соединение";
	case WSAECONNABORTED:       return "WSAECONNABORTED: Программный отказ связи";
	case WSAECONNRESET:         return "WSAECONNRESET: Связь восстановлена";
	case WSAETIMEDOUT:          return "WSAETIMEDOUT: Закончился отведенный интервал времени";
	case WSAECONNREFUSED:       return "WSAECONNREFUSED: Соединение отклонено";
	case WSAEHOSTDOWN:          return "WSAEHOSTDOWN: Хост в неработоспособном состоянии";
	case WSAEHOSTUNREACH:       return "WSAEHOSTUNREACH: Нет маршрута для хоста";
	case WSASYSNOTREADY:        return "WSASYSNOTREADY: Сеть не доступна";
	case WSAVERNOTSUPPORTED:    return "WSAVERNOTSUPPORTED: Данная версия недоступна";
	case WSANOTINITIALISED:     return "WSANOTINITIALISED: Не выполнена инициализация WS2_32.DLL";
	case WSAHOST_NOT_FOUND:     return "WSAHOST_NOT_FOUND: Хост не найден";
	case WSATRY_AGAIN:          return "WSATRY_AGAIN: Неавторизированный хост не найден";
	case WSANO_RECOVERY:        return "WSANO_RECOVERY: Неопределенная ошибка";
	case WSANO_DATA:            return "WSANO_DATA: Нет записи запрошенного типа";
	default:                    return "Ошибка Winsock2, код " + to_string(code);
	}
}

string SetErrorMsgText(string msgText, int code)
{
	return msgText + GetErrorMsgText(code);
}

bool GetServerByName(char* name, char* call, struct sockaddr* from, int* flen)
{
	SOCKET sS;
	if ((sS = socket(AF_INET, SOCK_DGRAM, 0)) == INVALID_SOCKET)
		throw SetErrorMsgText("socket: ", WSAGetLastError());

	int timeout = 3000;
	if (setsockopt(sS, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout)) == SOCKET_ERROR)
	{
		int err = WSAGetLastError();
		closesocket(sS);
		throw SetErrorMsgText("setsockopt: ", err);
	}

	struct hostent* host = gethostbyname(name);
	if (host == NULL || host->h_addr_list[0] == NULL)
	{
		int err = WSAGetLastError();
		closesocket(sS);
		cout << "GetServerByName: имя \"" << name << "\" не разрешается: "
			<< GetErrorMsgText(err) << endl;
		return false;
	}

	cout << "Имя \"" << name << "\" разрешено в адрес "
		<< inet_ntoa(*(in_addr*)host->h_addr_list[0]) << endl;

	SOCKADDR_IN server;
	memcpy(&server, from, sizeof(server));
	server.sin_family = AF_INET;
	memcpy(&server.sin_addr, host->h_addr_list[0], host->h_length);

	if (sendto(sS, call, (int)strlen(call) + 1, 0, (sockaddr*)&server, sizeof(server)) == SOCKET_ERROR)
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

	char name[256];
	if (argc > 1)
	{
		strncpy(name, argv[1], sizeof(name) - 1);
		name[sizeof(name) - 1] = '\0';
	}
	else if (gethostname(name, sizeof(name)) == SOCKET_ERROR)
	{
		cout << "Ошибка gethostname: " << GetErrorMsgText(WSAGetLastError()) << endl;
		return 1;
	}

	char call[32];
	strncpy(call, (argc > 2) ? argv[2] : "Hello", sizeof(call) - 1);
	call[sizeof(call) - 1] = '\0';

	short port = (short)((argc > 3) ? atoi(argv[3]) : 2000);

	WSADATA wsaData;
	SOCKADDR_IN serv;
	memset(&serv, 0, sizeof(serv));
	serv.sin_family = AF_INET;
	serv.sin_port = htons(port);
	int lserv = sizeof(serv);

	try
	{
		if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
			throw SetErrorMsgText("Startup: ", WSAGetLastError());

		cout << "ClientS: поиск сервера \"" << call << "\" на компьютере \"" << name
			<< "\", порт " << port << endl;

		if (GetServerByName(name, call, (struct sockaddr*)&serv, &lserv))
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
