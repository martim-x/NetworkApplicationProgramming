// Лабораторная работа № 3. Обмен данными без установки соединения (UDP)
// Серверная часть (ServerU). Задания 1, 2, 4, 5, 7, 8
// Отличия от ServerT (лаба 2): сокет SOCK_DGRAM, нет listen/accept,
// обмен функциями recvfrom/sendto с адресом клиента в каждом вызове.
// ПОМЕТКА (отклонение от исходного кода): VS помечает inet_ntoa как deprecated
// (рекомендует inet_ntop). Код пособия оставлен как есть — предупреждение отключено макросом.
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <string>
#include <iostream>
#include <cstdlib>
#include <ctime>
#include <map>
#include "Winsock2.h"
#pragma comment(lib, "WS2_32.lib")
using namespace std;

// Задание 1: функции обработки ошибок, разработанные в практической работе № 2
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

// Задание 1
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
	map<string, int> messageCounts; // Задание 8: счётчики сообщений по клиентам — ключ IP-адрес (строка), значение число сообщений
	clock_t startTime, endTime;

	try
	{
		// Задание 1 (блок 1): инициализация библиотеки WS2_32.DLL
		if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
			throw SetErrorMsgText("Startup:", WSAGetLastError());

		// Задание 1 (блок 2): сокет, ориентированный на сообщения — SOCK_DGRAM
		// (в ServerT был SOCK_STREAM; listen и accept здесь отсутствуют)
		if ((sS = socket(AF_INET, SOCK_DGRAM, 0)) == INVALID_SOCKET)
			throw SetErrorMsgText("socket:", WSAGetLastError());

		// Задание 2 (блок 2): связывание сокета, номер серверного сокета — 2000
		SOCKADDR_IN serverAddress;
		serverAddress.sin_family = AF_INET;
		serverAddress.sin_port = htons(2000);
		serverAddress.sin_addr.s_addr = INADDR_ANY;

		if (bind(sS, (sockaddr*)&serverAddress, sizeof(serverAddress)) == SOCKET_ERROR)
			throw SetErrorMsgText("bind:", WSAGetLastError());

		cout << "ServerU: waiting for datagrams on port 2000..." << endl;

		// Задание 8: сервер обслуживает клиентов по очереди —
		// после завершения обмена с одним готов принять сообщения следующего
		while (true)
		{
			SOCKADDR_IN clientAddress;
			int clientAddressSize = sizeof(clientAddress);
			bool sessionStarted = false; // время обмена меряется от первой дейтаграммы клиента, а не от ожидания в эфире
			startTime = clock();

			// Задание 7: обмен с клиентом до нулевого сообщения
			while (true)
			{
				// Задание 2 (блок 3): recvfrom переводит сервер в состояние ожидания
				// ПОМЕТКА (отклонение от исходного кода): в пособии и helper/ServerU_lab3
				// recvfrom(..., sizeof(ibuf), ...). Здесь sizeof(ibuf) - 1 и ibuf[lb] = '\0':
				// recvfrom не записывает нуль-терминатор, без резерва strlen и cout
				// читали бы мусор за границей принятых данных (ошибка, исправлявшаяся в лабе 2).
				lb = recvfrom(sS, ibuf, sizeof(ibuf) - 1, 0, (sockaddr*)&clientAddress, &clientAddressSize);
				if (lb == SOCKET_ERROR)
					throw SetErrorMsgText("recvfrom:", WSAGetLastError());

				ibuf[lb] = '\0';

				// Задание 7: нулевое сообщение — конец обмена с этим клиентом
				if (ibuf[0] == '\0')
					break;

				if (!sessionStarted)
				{
					startTime = clock(); // первая дейтаграмма сеанса — начало обмена
					sessionStarted = true;
				}

				// Задание 8: счётчик по IP-адресу отправителя — сообщения чередующихся
				// клиентов учитываются раздельно (ключ карты — IP-строка, значение — счётчик)
				messageCounts[inet_ntoa(clientAddress.sin_addr)]++;

				// Задание 8: вывод отправителя — видно чередование сообщений двух клиентов
				cout << "From " << inet_ntoa(clientAddress.sin_addr)
					<< ":" << ntohs(clientAddress.sin_port)
					<< ": " << ibuf << endl;

				// Задание 5 (блок 4): пересылка полученного сообщения обратно клиенту
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
			messageCounts.erase(finishedIp); // счётчик завершившегося клиента сброшен к следующему сеансу
		}

		// Задание 1 (блок 5): закрытие сокета и освобождение WS2_32.DLL
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
