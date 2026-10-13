// Лабораторная работа № 3. Демонстрация потери сообщений UDP-сервером (вопрос 12)
// ClientUFlood: штормовой клиент — посылает N дейтаграмм подряд, НЕ ожидая
// эхо-ответов (fire-and-forget). Медленный сервер (ServerULoss, Sleep на каждое
// сообщение) не успевает вынимать дейтаграммы из очереди приёма — она
// переполняется, и ОС теряет всё, что не поместилось.
// Почему клиент не ждёт ответ: в режиме «отправил – принял» (задание 7) в полёте
// всегда максимум одна дейтаграмма, очередь сервера переполниться не может и
// потерь не бывает. Замер времени здесь — только на фазе отправки: клиент
// печатает, за сколько отправил N дейтаграмм. Сколько реально принял сервер —
// печатает сервер; разность и есть потерянные дейтаграммы. Потоки не нужны:
// каждая сторона меряет свою фазу, параллельного замера нет.
// Запуск: ClientUFlood [ip] [порт] [число_сообщений]  (по умолчанию 127.0.0.1 2000 5000)
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#include <string>
#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <ctime>
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
	const char* serverIp = (argc > 1) ? argv[1] : "127.0.0.1";
	int serverPort = (argc > 2) ? atoi(argv[2]) : 2000;
	int messageTotal = (argc > 3) ? atoi(argv[3]) : 5000;

	SOCKET cC;
	WSADATA wsaData;
	char sendBuffer[50];
	clock_t startTime, endTime;

	try
	{
		if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
			throw SetErrorMsgText("Startup:", WSAGetLastError());

		// bind не нужен — Windows выделяет клиенту эфемерный порт
		if ((cC = socket(AF_INET, SOCK_DGRAM, 0)) == INVALID_SOCKET)
			throw SetErrorMsgText("socket:", WSAGetLastError());

		SOCKADDR_IN serverAddress;
		serverAddress.sin_family = AF_INET;
		serverAddress.sin_port = htons(serverPort);
		serverAddress.sin_addr.s_addr = inet_addr(serverIp);

		cout << "ClientUFlood: sending " << messageTotal << " datagrams to "
			<< serverIp << ":" << serverPort << " (fire-and-forget, echoes are not read)..." << endl;

		// Штормовая отправка: без recvfrom между sendto, очередь сервера
		// не успевает опустошаться и переполняется
		startTime = clock();
		for (int i = 1; i <= messageTotal; i++)
		{
			sprintf(sendBuffer, "Flood %05d", i);
			if (sendto(cC, sendBuffer, (int)(strlen(sendBuffer) + 1), 0,
				(sockaddr*)&serverAddress, sizeof(serverAddress)) == SOCKET_ERROR)
				throw SetErrorMsgText("sendto:", WSAGetLastError());
		}
		endTime = clock();

		double seconds = ((double)(endTime - startTime)) / CLOCKS_PER_SEC;
		cout << "Sent " << messageTotal << " datagrams in " << seconds << " seconds"
			<< " (" << (int)(messageTotal / (seconds > 0.001 ? seconds : 0.001)) << " per second)." << endl;

		// Даём серверу доразобрать остаток очереди, прежде чем закрыть порт:
		// иначе его эхо уйдёт в закрытый порт (на сервере это обработано как
		// WSAECONNRESET, но лучше не провоцировать)
		cout << "Waiting 15 seconds for the server to drain its queue..." << endl;
		Sleep(15000);

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
