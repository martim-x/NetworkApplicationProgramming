#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <string>
#include <iostream>
#include <cstdlib>
#include <ctime>
#include <map>
#include "Winsock2.h"
#pragma comment(lib, "WS2_32.lib")
using namespace std;

string GetErrorMsgText(int code)
{
	switch (code)
	{
	case WSAEINTR:          return "WSAEINTR: Работа функции прервана";
	case WSAEACCES:         return "WSAEACCES: Разрешение отвергнуто";
	case WSAEFAULT:         return "WSAEFAULT: Ошибочный адрес";
	case WSAEINVAL:         return "WSAEINVAL: Ошибка в аргументе";
	case WSAEMFILE:         return "WSAEMFILE: Слишком много файлов открыто";
	case WSAENOTSOCK:       return "WSAENOTSOCK: Сокет задан неправильно";
	case WSAEADDRINUSE:     return "WSAEADDRINUSE: Адрес уже используется";
	case WSAEADDRNOTAVAIL:  return "WSAEADDRNOTAVAIL: Запрошенный адрес не может быть использован";
	case WSAENETDOWN:       return "WSAENETDOWN: Сеть отключена";
	case WSAENETRESET:      return "WSAENETRESET: Сеть разорвала соединение";
	case WSAECONNABORTED:   return "WSAECONNABORTED: Программный отказ связи";
	case WSAECONNRESET:     return "WSAECONNRESET: Соединение сброшено";
	case WSAENOTCONN:       return "WSAENOTCONN: Сокет не подключен";
	case WSAETIMEDOUT:      return "WSAETIMEDOUT: Закончился отведенный интервал времени";
	case WSAECONNREFUSED:   return "WSAECONNREFUSED: Соединение отклонено";
	case WSANOTINITIALISED: return "WSANOTINITIALISED: Не выполнена инициализация WS2_32.DLL";
	default:                return "Ошибка Winsock, код " + to_string(code);
	}
}

string SetErrorMsgText(string msgText, int code)
{
	return msgText + GetErrorMsgText(code);
}

int main()
{
	SOCKET sS;
	WSADATA wsaData;
	char ibuf[50];
	int lb;
	map<string, int> messageCounts; 	clock_t startTime, endTime;

	try
	{
				if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
			throw SetErrorMsgText("Startup:", WSAGetLastError());

						if ((sS = socket(AF_INET, SOCK_DGRAM, 0)) == INVALID_SOCKET)
			throw SetErrorMsgText("socket:", WSAGetLastError());

				SOCKADDR_IN serverAddress;
		serverAddress.sin_family = AF_INET;
		serverAddress.sin_port = htons(2000);
		serverAddress.sin_addr.s_addr = INADDR_ANY;

		if (bind(sS, (sockaddr*)&serverAddress, sizeof(serverAddress)) == SOCKET_ERROR)
			throw SetErrorMsgText("bind:", WSAGetLastError());

		cout << "ServerU: waiting for datagrams on port 2000..." << endl;

						while (true)
		{
			SOCKADDR_IN clientAddress;
			int clientAddressSize = sizeof(clientAddress);
			bool sessionStarted = false; 			startTime = clock();

						while (true)
			{
																								lb = recvfrom(sS, ibuf, sizeof(ibuf) - 1, 0, (sockaddr*)&clientAddress, &clientAddressSize);
				if (lb == SOCKET_ERROR)
					throw SetErrorMsgText("recvfrom:", WSAGetLastError());

				ibuf[lb] = '\0';

								if (ibuf[0] == '\0')
					break;

				if (!sessionStarted)
				{
					startTime = clock(); 					sessionStarted = true;
				}

												messageCounts[inet_ntoa(clientAddress.sin_addr)]++;

								cout << "From " << inet_ntoa(clientAddress.sin_addr)
					<< ":" << ntohs(clientAddress.sin_port)
					<< ": " << ibuf << endl;

								if (sendto(sS, ibuf, lb, 0, (sockaddr*)&clientAddress, clientAddressSize) == SOCKET_ERROR)
					throw SetErrorMsgText("sendto:", WSAGetLastError());
			}
			endTime = clock();

			string finishedIp = inet_ntoa(clientAddress.sin_addr);
			cout << "\nClient " << finishedIp
				<< ":" << ntohs(clientAddress.sin_port)
				<< " finished. Messages: " << messageCounts[finishedIp]
				<< ", exchange time: " << ((double)(endTime - startTime)) / CLOCKS_PER_SEC
				<< " seconds.\n"
				<< "Waiting for the next client...\n" << endl;
			messageCounts.erase(finishedIp); 		}

				if (closesocket(sS) == SOCKET_ERROR)
			throw SetErrorMsgText("closesocket:", WSAGetLastError());
		if (WSACleanup() == SOCKET_ERROR)
			throw SetErrorMsgText("Cleanup:", WSAGetLastError());
	}
	catch (string errorMsgText)
	{
		cout << endl << "WSAGetLastError: " << errorMsgText;
	}

	return 0;
}
