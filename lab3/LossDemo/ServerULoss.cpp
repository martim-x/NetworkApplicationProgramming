// Лабораторная работа № 3. Демонстрация потери сообщений UDP-сервером (вопрос 12)
// ServerULoss: эхо-сервер с искусственно медленной обработкой — Sleep(delay)
// после каждого принятого сообщения. Клиент-шторм (ClientUFlood) шлёт дейтаграммы
// быстрее, чем сервер успевает их вынимать, очередь приёма сокета (SO_RCVBUF)
// переполняется, и ОС молча отбрасывает лишние дейтаграммы. Сервер печатает,
// сколько сообщений реально принял, — разность «отправлено клиентом – принято
// сервером» и есть потерянные дейтаграммы.
// Отличия от ServerU: Sleep(delay) на каждое сообщение; уменьшенный буфер
// приёма SO_RCVBUF (демо не зависит от буфера по умолчанию); тайм-аут приёма
// SO_RCVTIMEO — конец сеанса по тишине в эфире, т.к. нулевое сообщение-маркер
// при переполненной очереди само может быть потеряно.
// Запуск: ServerULoss [порт] [задержка_ms]  (по умолчанию 2000 и 20)
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <string>
#include <iostream>
#include <cstdlib>
#include <ctime>
#include <map>
#include "Winsock2.h"
#include <windows.h> // Sleep
#pragma comment(lib, "WS2_32.lib")
using namespace std;

// Функции обработки ошибок из практической работы № 2
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
	map<string, int> messageCounts; // счётчики принятых сообщений: ключ IP-адрес клиента (строка), значение счётчик
	clock_t startTime, endTime;

	try
	{
		// Инициализация библиотеки WS2_32.DLL
		if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
			throw SetErrorMsgText("Startup:", WSAGetLastError());

		if ((sS = socket(AF_INET, SOCK_DGRAM, 0)) == INVALID_SOCKET)
			throw SetErrorMsgText("socket:", WSAGetLastError());

		// Буфер приёма уменьшен намеренно: с 8 КБ очередь переполняется уже на
		// нескольких десятках мелких дейтаграмм, потеря видна при любом N
		// (с буфером по умолчанию 64 КБ понадобились бы тысячи сообщений)
		int rcvBufBytes = 8192;
		if (setsockopt(sS, SOL_SOCKET, SO_RCVBUF, (char*)&rcvBufBytes, sizeof(rcvBufBytes)) == SOCKET_ERROR)
			throw SetErrorMsgText("setsockopt RCVBUF:", WSAGetLastError());

		// Тайм-аут приёма 3 с: если дейтаграммы прекратились — сеанс окончен.
		// Нулевое сообщение-маркер из задания 7 здесь не используется:
		// при переполненной очереди оно само попадает под потерю
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

		// Сервер обслуживает клиентов по очереди
		while (true)
		{
			SOCKADDR_IN clientAddress;
			int clientAddressSize = sizeof(clientAddress);
			bool sessionHasDatagrams = false; // пришла ли хоть одна дейтаграмма в текущем сеансе
			int sessionCount = 0;
			startTime = clock();

			while (true)
			{
				lb = recvfrom(sS, ibuf, sizeof(ibuf) - 1, 0, (sockaddr*)&clientAddress, &clientAddressSize);
				if (lb == SOCKET_ERROR)
				{
					int err = WSAGetLastError();
					// Тишина 3 с: если сеанс уже начался — клиент закончил, иначе ждём дальше
					if (err == WSAETIMEDOUT)
					{
						if (sessionHasDatagrams)
							break;
						continue;
					}
					// Особенность UDP в Windows: sendto в уже закрытый порт клиента
					// возвращает ICMP «порт недоступен», и следующий recvfrom даёт
					// WSAECONNRESET. Для демо потерь это не ошибка — пропускаем
					if (err == WSAECONNRESET)
						continue;
					throw SetErrorMsgText("recvfrom:", err);
				}

				ibuf[lb] = '\0';
				sessionHasDatagrams = true;

				// Нулевое сообщение из задания 7 тоже поддерживаем — с обычным
				// клиентом ClientU этот сервер работает как обычный ServerU
				if (ibuf[0] == '\0')
					break;

				messageCounts[inet_ntoa(clientAddress.sin_addr)]++;
				sessionCount++;

				// Медленная обработка: пока сервер спит, очередь приёма
				// переполняется, и приходящие дейтаграммы ОС отбрасывает
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
