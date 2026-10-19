#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <string>
#include <iostream>
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
		int port = (argc > 1) ? atoi(argv[1]) : 2000;

	SOCKET listenSocket;
	SOCKET clientSocket;
	WSADATA wsaData;
	char buffer[50];
	int receivedBytes;
	int messageCount;
	clock_t startTime, endTime;

	try
	{
				if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
			throw SetErrorMsgText("Startup:", WSAGetLastError());

				if ((listenSocket = socket(AF_INET, SOCK_STREAM, 0)) == INVALID_SOCKET)
			throw SetErrorMsgText("socket:", WSAGetLastError());

				SOCKADDR_IN serverAddress;
		serverAddress.sin_family = AF_INET;
		serverAddress.sin_port = htons(port);
		serverAddress.sin_addr.s_addr = INADDR_ANY;

		if (bind(listenSocket, (sockaddr*)&serverAddress, sizeof(serverAddress)) == SOCKET_ERROR)
			throw SetErrorMsgText("bind:", WSAGetLastError());

		if (listen(listenSocket, SOMAXCONN) == SOCKET_ERROR)
			throw SetErrorMsgText("listen:", WSAGetLastError());

		cout << "ServerT: waiting for clients on port " << port << "..." << endl;

				while (true)
		{
			SOCKADDR_IN clientAddress;
			int clientAddressSize = sizeof(clientAddress);

						clientSocket = accept(listenSocket, (sockaddr*)&clientAddress, &clientAddressSize);
			if (clientSocket == INVALID_SOCKET)
				throw SetErrorMsgText("accept:", WSAGetLastError());

						cout << "\nClient connected." << endl;
			cout << "Client IP:    " << inet_ntoa(clientAddress.sin_addr) << endl;
			cout << "Client port:  " << ntohs(clientAddress.sin_port) << "\n\n";

						messageCount = 0;
			startTime = clock();
			while (true)
			{
																				receivedBytes = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
				if (receivedBytes == SOCKET_ERROR)
					throw SetErrorMsgText("recv:", WSAGetLastError());

								if (receivedBytes == 0)
					break;

				buffer[receivedBytes] = '\0';
				messageCount++;
				cout << "Received: " << buffer << endl;

								if (send(clientSocket, buffer, receivedBytes, 0) == SOCKET_ERROR)
					throw SetErrorMsgText("send:", WSAGetLastError());
			}
			endTime = clock();

			cout << "\nClient disconnected. Messages: " << messageCount << endl;
			cout << "Exchange time: " << ((double)(endTime - startTime)) / CLOCKS_PER_SEC
				<< " seconds.\n" << endl;

						if (closesocket(clientSocket) == SOCKET_ERROR)
				throw SetErrorMsgText("closesocket:", WSAGetLastError());
		}

		if (closesocket(listenSocket) == SOCKET_ERROR)
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
