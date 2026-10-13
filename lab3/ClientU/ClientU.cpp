// Лабораторная работа № 3. Обмен данными без установки соединения (UDP)
// Клиентская часть (ClientU). Задания 3, 4, 5, 6, 7, 8
// Отличия от ClientT (лаба 2): сокет SOCK_DGRAM, нет connect,
// обмен функциями sendto/recvfrom с адресом сервера в каждом вызове.
// ПОМЕТКА (отклонение от исходного кода): VS помечает inet_addr и sprintf как deprecated
// (рекомендует inet_pton и sprintf_s). Код пособия оставлен как есть — предупреждения отключены макросами.
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#include <string>
#include <iostream>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include "Winsock2.h"
#pragma comment(lib, "WS2_32.lib")
using namespace std;

// Задание 3: функции обработки ошибок, разработанные в практической работе № 2
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

// Задание 3
string SetErrorMsgText(string msgText, int code)
{
	return msgText + GetErrorMsgText(code);
}

int main(int argc, char* argv[])
{
	// Задание 6: адрес сервера задаётся параметрами запуска —
	// программы можно расположить на разных компьютерах локальной сети
	// (по умолчанию 127.0.0.1:2000 — петля, как требуется в задании 3)
	const char* serverIp = (argc > 1) ? argv[1] : "127.0.0.1";
	int serverPort = (argc > 2) ? atoi(argv[2]) : 2000;

	SOCKET cC;
	WSADATA wsaData;
	char receiveBuffer[50];
	char sendBuffer[50];
	int lb;
	int serverAddressSize;
	int messageTotal;
	int receivedCount;
	clock_t startTime, endTime;

	try
	{
		// Задание 3 (блок 1): инициализация библиотеки WS2_32.DLL
		if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
			throw SetErrorMsgText("Startup:", WSAGetLastError());

		// Задание 3 (блок 2): сокет, ориентированный на сообщения — SOCK_DGRAM;
		// bind не нужен — Windows выделяет клиенту эфемерный порт
		if ((cC = socket(AF_INET, SOCK_DGRAM, 0)) == INVALID_SOCKET)
			throw SetErrorMsgText("socket:", WSAGetLastError());

		// Задание 3: параметры сокета сервера для параметра to команды sendto
		SOCKADDR_IN serverAddress;
		serverAddress.sin_family = AF_INET;
		serverAddress.sin_port = htons(serverPort);
		serverAddress.sin_addr.s_addr = inet_addr(serverIp);
		serverAddressSize = sizeof(serverAddress);

		// Задание 7: число сообщений с консоли (как в заданиях 13, 14 практической работы № 2)
		cout << "Enter number of messages: ";
		cin >> messageTotal;

		// Задание 7: последовательная пересылка данных от клиента серверу и обратно
		receivedCount = 0;
		startTime = clock();
		for (int i = 1; i <= messageTotal; i++)
		{
			// Задание 3: пересылка сообщения Hello from ClientU
			sprintf(sendBuffer, "Hello from ClientU %03d", i);

			// ПОМЕТКА (отклонение от исходного кода): добавлено явное приведение (int) —
			// strlen возвращает size_t (64 бита), а третий параметр sendto ожидает int,
			// иначе VS предупреждает о возможной потере данных.
			if (sendto(cC, sendBuffer, (int)(strlen(sendBuffer) + 1), 0,
				(sockaddr*)&serverAddress, sizeof(serverAddress)) == SOCKET_ERROR)
				throw SetErrorMsgText("sendto:", WSAGetLastError());

			// Задание 5 (блок 4): приём сообщения, пересланного сервером обратно
			// ПОМЕТКА (отклонение от исходного кода): в пособии и helper/ClientU_lab3
			// recvfrom(..., sizeof(ibuf), ...). Здесь sizeof(receiveBuffer) - 1 и
			// receiveBuffer[lb] = '\0': recvfrom не записывает нуль-терминатор, без резерва
			// strlen и cout читали бы мусор за границей принятых данных (ошибка из лабы 2).
			lb = recvfrom(cC, receiveBuffer, sizeof(receiveBuffer) - 1, 0,
				(sockaddr*)&serverAddress, &serverAddressSize);
			if (lb == SOCKET_ERROR)
				throw SetErrorMsgText("recvfrom:", WSAGetLastError());

			receiveBuffer[lb] = '\0';
			receivedCount++;
			cout << "Received: " << receiveBuffer << endl;
		}
		endTime = clock();

		// Задание 7: оценка скорости передачи — сравнить с результатами лабы 2 (TCP)
		cout << "\nExchange time for " << messageTotal << " messages: "
			<< ((double)(endTime - startTime)) / CLOCKS_PER_SEC << " seconds.\n";

		// Задание 8: оценка числа переданных и полученных сообщений —
		// при потере дейтаграмм UDP receivedCount окажется меньше messageTotal
		cout << "Sent: " << messageTotal << ", received: " << receivedCount << endl;

		// Задание 7: нулевое сообщение — конец обмена (как в заданиях 13, 14 работы № 2);
		// в UDP пересылается дейтаграмма из одного байта '\0'
		if (sendto(cC, "", 1, 0, (sockaddr*)&serverAddress, sizeof(serverAddress)) == SOCKET_ERROR)
			throw SetErrorMsgText("sendto:", WSAGetLastError());

		// Задание 3 (блок 5): закрытие сокета и освобождение WS2_32.DLL
		if (closesocket(cC) == SOCKET_ERROR)
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
