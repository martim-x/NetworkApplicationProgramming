



#define _WINSOCK_DEPRECATED_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#include <string>
#include <iostream>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <ctime>
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
	
	const char* serverIp = (argc > 1) ? argv[1] : "127.0.0.1";
	
	int serverPort = (argc > 2) ? atoi(argv[2]) : 2000;

	SOCKET clientSocket;
	WSADATA wsaData;
	char receiveBuffer[50];
	char sendBuffer[50];
	int receivedBytes;
	int messageTotal = (argc > 3) ? atoi(argv[3]) : 0;
	int messageNumber = 1;
	clock_t startTime, endTime;

	try
	{
		
		if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
			throw SetErrorMsgText("Startup:", WSAGetLastError());

		
		if ((clientSocket = socket(AF_INET, SOCK_STREAM, 0)) == INVALID_SOCKET)
			throw SetErrorMsgText("socket:", WSAGetLastError());

		
		SOCKADDR_IN serverAddress;
		serverAddress.sin_family = AF_INET;
		serverAddress.sin_port = htons(serverPort);
		
		serverAddress.sin_addr.s_addr = inet_addr(serverIp);

		
		if (connect(clientSocket, (sockaddr*)&serverAddress, sizeof(serverAddress)) == SOCKET_ERROR)
			throw SetErrorMsgText("connect:", WSAGetLastError());

		
		if (messageTotal <= 0)
		{
			cout << "Enter number of messages: ";
			cin >> messageTotal;
		}

		
		startTime = clock();
		for (int i = 0; i < messageTotal; i++)
		{
			sprintf(sendBuffer, "Hello from Client %03d", messageNumber);

			
			
			
			
			if (send(clientSocket, sendBuffer, (int)(strlen(sendBuffer) + 1), 0) == SOCKET_ERROR)
				throw SetErrorMsgText("send:", WSAGetLastError());

			
			
			
			
			receivedBytes = recv(clientSocket, receiveBuffer, sizeof(receiveBuffer) - 1, 0);
			if (receivedBytes == SOCKET_ERROR)
				throw SetErrorMsgText("recv:", WSAGetLastError());

			receiveBuffer[receivedBytes] = '\0';
			cout << "Received: " << receiveBuffer << endl;

			
			messageNumber++;
		}
		endTime = clock();

		
		cout << "\nExchange time for " << messageTotal << " messages: "
			<< ((double)(endTime - startTime)) / CLOCKS_PER_SEC << " seconds.\n";

		
		if (send(clientSocket, "", 0, 0) == SOCKET_ERROR)
			throw SetErrorMsgText("send:", WSAGetLastError());

		if (closesocket(clientSocket) == SOCKET_ERROR)
			throw SetErrorMsgText("closesocket:", WSAGetLastError());
		if (WSACleanup() == SOCKET_ERROR)
			throw SetErrorMsgText("Cleanup: ", WSAGetLastError());
	}
	catch (string errorMsgText)
	{
		cout << endl << "WSAGetLastError: " << errorMsgText;
	}

	return 0;
}
