











#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <string>
#include <iostream>
#include <cstdlib>
#include <ctime>
#include <map>
#include "Winsock2.h"
#include <windows.h> 
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

int main(int argc, char* argv[])
{
	SOCKET sS;
	WSADATA wsaData;
	char ibuf[50];
	int lb;
	int port = (argc > 1) ? atoi(argv[1]) : 2000;
	int delayMs = (argc > 2) ? atoi(argv[2]) : 20;
	map<string, int> messageCounts; 
	clock_t startTime, endTime;

	try
	{
		
		if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
			throw SetErrorMsgText("Startup:", WSAGetLastError());

		if ((sS = socket(AF_INET, SOCK_DGRAM, 0)) == INVALID_SOCKET)
			throw SetErrorMsgText("socket:", WSAGetLastError());

		
		
		
		int rcvBufBytes = 8192;
		if (setsockopt(sS, SOL_SOCKET, SO_RCVBUF, (char*)&rcvBufBytes, sizeof(rcvBufBytes)) == SOCKET_ERROR)
			throw SetErrorMsgText("setsockopt RCVBUF:", WSAGetLastError());

		
		
		
		DWORD recvTimeoutMs = 3000;
		if (setsockopt(sS, SOL_SOCKET, SO_RCVTIMEO, (char*)&recvTimeoutMs, sizeof(recvTimeoutMs)) == SOCKET_ERROR)
			throw SetErrorMsgText("setsockopt RCVTIMEO:", WSAGetLastError());

		SOCKADDR_IN serverAddress;
		serverAddress.sin_family = AF_INET;
		serverAddress.sin_port = htons(port);
		serverAddress.sin_addr.s_addr = INADDR_ANY;

		if (bind(sS, (sockaddr*)&serverAddress, sizeof(serverAddress)) == SOCKET_ERROR)
			throw SetErrorMsgText("bind:", WSAGetLastError());

		cout << "ServerULoss: port " << port << ", Sleep " << delayMs
			<< " ms per message, recv buffer " << rcvBufBytes
			<< " bytes, session timeout " << recvTimeoutMs << " ms..." << endl;

		
		while (true)
		{
			SOCKADDR_IN clientAddress;
			int clientAddressSize = sizeof(clientAddress);
			bool sessionHasDatagrams = false; 
			bool sessionStarted = false;      
			int sessionCount = 0;
			startTime = clock();

			while (true)
			{
				lb = recvfrom(sS, ibuf, sizeof(ibuf) - 1, 0, (sockaddr*)&clientAddress, &clientAddressSize);
				if (lb == SOCKET_ERROR)
				{
					int err = WSAGetLastError();
					
					if (err == WSAETIMEDOUT)
					{
						if (sessionHasDatagrams)
							break;
						continue;
					}
					
					
					
					if (err == WSAECONNRESET)
						continue;
					throw SetErrorMsgText("recvfrom:", err);
				}

				ibuf[lb] = '\0';
				sessionHasDatagrams = true;

				
				
				if (ibuf[0] == '\0')
					break;

				messageCounts[inet_ntoa(clientAddress.sin_addr)]++;
				sessionCount++;

				if (!sessionStarted)
				{
					startTime = clock(); 
					sessionStarted = true;
				}

				
				
				Sleep(delayMs);

				if (sendto(sS, ibuf, lb, 0, (sockaddr*)&clientAddress, clientAddressSize) == SOCKET_ERROR)
					throw SetErrorMsgText("sendto:", WSAGetLastError());

				if (sessionCount % 25 == 0)
					cout << "received " << sessionCount << " so far..." << endl;
			}
			endTime = clock();

			string finishedIp = inet_ntoa(clientAddress.sin_addr);
			cout << "\nClient " << finishedIp
				<< ":" << ntohs(clientAddress.sin_port)
				<< " finished. Received: " << messageCounts[finishedIp]
				<< " (client sent its own total - the difference is lost datagrams)"
				<< ", processing time: " << ((double)(endTime - startTime)) / CLOCKS_PER_SEC
				<< " seconds.\n"
				<< "Waiting for the next client...\n" << endl;
			messageCounts.erase(finishedIp);
		}

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
