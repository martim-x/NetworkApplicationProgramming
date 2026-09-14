// Лабораторная работа № 2. Обмен данными по TCP-соединению
// Серверная часть распределенного приложения (ServerT_lab2)
// Задания 1-6, 10, 11, 14, 15
#include <iostream>
#include <clocale>
#include <ctime>
#include "Winsock2.h"
#pragma comment(lib, "WS2_32.lib")
#pragma warning(disable:4996)
using namespace std;

string  GetErrorMsgText(int code)    // cформировать текст ошибки
{
	string msgText;
	switch (code)                      // проверка кода возврата
	{
	case WSAEINTR: msgText = "WSAEINTR: Работа функции прервана ";         break;
	case WSAEACCES: msgText = "WSAEACCES: Разрешение отвергнуто";        break;
	case WSAEFAULT: msgText = "WSAEFAULT: Ошибочный адрес"; break;
	case WSAEINVAL: msgText = "WSAEINVAL: Ошибка в аргументе"; break;
	case WSAEMFILE: msgText = "WSAEMFILE: Слишком много файлов открыто"; break;
	case WSAEWOULDBLOCK:msgText = "WSAEWOULDBLOCK: Ресурс временно недоступен"; break;
	case WSAEINPROGRESS:msgText = "WSAEINPROGRESS: Операция в процессе развития"; break;
	case WSAEALREADY:msgText = "WSAEALREADY: Операция уже выполняется "; break;
	case WSAENOTSOCK:msgText = "WSAENOTSOCK: Сокет задан неправильно   "; break;
	case WSAEDESTADDRREQ:msgText = "WSAEDESTADDRREQ: Требуется адрес расположения "; break;
	case WSAEMSGSIZE:msgText = "WSAEMSGSIZE: Сообщение слишком длинное "; break;
	case WSAEPROTOTYPE:msgText = "WSAEPROTOTYPE: Неправильный тип протокола для сокета "; break;
	case WSAENOPROTOOPT:msgText = "WSAENOPROTOOPT: Ошибка в опции протокола"; break;
	case WSAEPROTONOSUPPORT:msgText = "WSAEPROTONOSUPPORT: Протокол не поддерживается "; break;
	case WSAESOCKTNOSUPPORT:msgText = "WSAESOCKTNOSUPPORT: Тип сокета не поддерживается "; break;
	case WSAEOPNOTSUPP:msgText = "WSAEOPNOTSUPP: Операция не поддерживается "; break;
	case WSAEPFNOSUPPORT:msgText = "WSAEPFNOSUPPORT: Тип протоколов не поддерживается "; break;
	case WSAEAFNOSUPPORT:msgText = "WSAEAFNOSUPPORT: Тип адресов не поддерживается протоколом"; break;
	case WSAEADDRINUSE:msgText = "WSAEADDRINUSE: Адрес уже используется "; break;
	case WSAEADDRNOTAVAIL:msgText = "WSAEADDRNOTAVAIL: Запрошенный адрес не может быть использован"; break;
	case WSAENETDOWN:msgText = "WSAENETDOWN: Сеть отключена "; break;
	case WSAENETUNREACH:msgText = "WSAENETUNREACH: Сеть не достижима"; break;
	case WSAENETRESET:msgText = "WSAENETRESET: Сеть разорвала соединение"; break;
	case WSAECONNABORTED:msgText = "WSAECONNABORTED: Программный отказ связи "; break;
	case WSAECONNRESET:msgText = "WSAECONNRESET: Соединение сброшено "; break;
	case WSAENOBUFS:msgText = "WSAENOBUFS: Не хватает памяти для буферов"; break;
	case WSAEISCONN:msgText = "WSAEISCONN: Сокет уже подключен"; break;
	case WSAENOTCONN:msgText = "WSAENOTCONN: Сокет не подключен"; break;
	case WSAESHUTDOWN:msgText = "WSAESHUTDOWN: Нельзя выполнить send: сокет завершил работу"; break;
	case WSAETIMEDOUT:msgText = "WSAETIMEDOUT: Закончился отведенный интервал  времени"; break;
	case WSAECONNREFUSED:msgText = "WSAECONNREFUSED: Соединение отклонено"; break;
	case WSAEHOSTDOWN:msgText = "WSAEHOSTDOWN: Хост в неработоспособном состоянии"; break;
	case WSAEHOSTUNREACH:msgText = "WSAEHOSTUNREACH: Нет маршрута для хоста"; break;
	case WSAEPROCLIM:msgText = "WSAEPROCLIM: Слишком много процессов "; break;
	case WSASYSNOTREADY:msgText = "WSASYSNOTREADY: Сеть не доступна "; break;
	case WSAVERNOTSUPPORTED:msgText = "WSAVERNOTSUPPORTED: Данная версия недоступна"; break;
	case WSANOTINITIALISED:msgText = "WSANOTINITIALISED: Не выполнена инициализация WS2_32.DLL"; break;
	case WSAEDISCON:msgText = "WSAEDISCON: Выполняется отключение"; break;
	case WSATYPE_NOT_FOUND:msgText = "WSATYPE_NOT_FOUND: Класс не найден"; break;
	case WSAHOST_NOT_FOUND:msgText = "WSAHOST_NOT_FOUND: Хост не найден"; break;
	case WSATRY_AGAIN:msgText = "WSATRY_AGAIN: Неавторизированный хост не найден"; break;
	case WSANO_RECOVERY:msgText = "WSANO_RECOVERY: Неопределенная  ошибка"; break;
	case WSANO_DATA:msgText = "WSANO_DATA: Нет записи запрошенного типа "; break;
	case WSA_INVALID_HANDLE:msgText = "WSA_INVALID_HANDLE: Указанный дескриптор события  с ошибкой"; break;
	case WSA_INVALID_PARAMETER:msgText = "WSA_INVALID_PARAMETER: Один или более параметров с ошибкой"; break;
	case WSA_IO_INCOMPLETE:msgText = "WSA_IO_INCOMPLETE: Объект ввода-вывода не в сигнальном состоянии"; break;
	case WSA_IO_PENDING:msgText = "WSA_IO_PENDING: Операция завершится позже"; break;
	case WSA_NOT_ENOUGH_MEMORY:msgText = "WSA_NOT_ENOUGH_MEMORY: Не достаточно памяти"; break;
	case WSA_OPERATION_ABORTED:msgText = "WSA_OPERATION_ABORTED: Операция отвергнута"; break;
	case WSAEINVALIDPROCTABLE:msgText = "WSAEINVALIDPROCTABLE: Ошибочный сервис"; break;
	case WSAEINVALIDPROVIDER:msgText = "WSAEINVALIDPROVIDER: Ошибка в версии сервиса"; break;
	case WSAEPROVIDERFAILEDINIT:msgText = "WSAEPROVIDERFAILEDINIT: Невозможно инициализировать сервис"; break;
	case WSASYSCALLFAILURE: msgText = "WSASYSCALLFAILURE: Аварийное завершение системного вызова"; break;
	default:                msgText = "***ERROR***";      break;
	};
	return msgText;
};

string SetErrorMsgText(string msgText, int code) //функция для обр-ки стандартных ошибок
{
	return  msgText + GetErrorMsgText(code);
};

int main()
{
	SOCKET  sS;          // серверный (слушающий) сокет
	SOCKET  cS;          // сокет подключившегося клиента (создается accept)
	WSADATA wsaData;     // структура для хранения инфы о библиотеке
	char    ibuf[50];    // буфер приема
	int     libuf;       // количество принятых байт
	int     cnt;         // счетчик принятых сообщений
	clock_t t1, t2;      // замер времени обмена (задание 14)

	try
	{
		// задание 3: ИНИЦИАЛИЗАЦИЯ БИБЛИОТЕКИ WINSOCK
		if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
			throw  SetErrorMsgText("Startup:", WSAGetLastError());

		// задание 4: СОЗДАНИЕ СОКЕТА, ОРИЕНТИРОВАННОГО НА ПОТОК
		if ((sS = socket(AF_INET, SOCK_STREAM, NULL)) == INVALID_SOCKET)
			throw  SetErrorMsgText("socket:", WSAGetLastError());

		// задание 5: ПАРАМЕТРЫ СОКЕТА sS
		SOCKADDR_IN serv;                    // структура параметров сокета
		serv.sin_family = AF_INET;           // используется IP-адресация (IPv4)
		serv.sin_port = htons(2000);         // порт 2000
		serv.sin_addr.s_addr = INADDR_ANY;   // любой собственный IP-адрес

		// задание 5: ПРИВЯЗКА СОКЕТА К АДРЕСУ И ПОРТУ
		if (bind(sS, (LPSOCKADDR)&serv, sizeof(serv)) == SOCKET_ERROR)
			throw  SetErrorMsgText("bind:", WSAGetLastError());

		// задание 5: ПРОСЛУШИВАНИЕ ВХОДЯЩИХ ПОДКЛЮЧЕНИЙ
		if (listen(sS, SOMAXCONN) == SOCKET_ERROR)
			throw  SetErrorMsgText("listen:", WSAGetLastError());

		cout << "ServerT: waiting for clients on port 2000..." << endl;

		// задание 15: после отключения клиента сервер принимает
		// следующего клиента и продолжает работу
		while (true)
		{
			SOCKADDR_IN c_adr;               // параметры сокета клиента
			int lcInt = sizeof(c_adr);

			// задание 6: ОЖИДАНИЕ ПОДКЛЮЧЕНИЯ (блокирует выполнение,
			// пока не подключится клиент)
			cS = accept(sS, (sockaddr*)&c_adr, &lcInt);
			if (cS == INVALID_SOCKET)
				throw  SetErrorMsgText("accept:", WSAGetLastError());

			// задание 10: IP-адрес и порт подключившегося клиента
			cout << "\nClient connected." << endl;
			cout << "Client IP:    " << inet_ntoa(c_adr.sin_addr) << endl;
			cout << "Client port:  " << ntohs(c_adr.sin_port) << "\n\n";

			// ОБМЕН ДАННЫМИ С КЛИЕНТОМ (задания 11, 14)
			cnt = 0;
			t1 = clock();
			while (true)
			{
				// recv использует сокет, созданный accept (а не sS!)
				libuf = recv(cS, ibuf, sizeof(ibuf), NULL);
				if (libuf == SOCKET_ERROR)
					throw  SetErrorMsgText("recv:", WSAGetLastError());

				// задание 14: сообщение нулевой длины — конец обмена
				if (libuf == 0)
					break;

				ibuf[libuf] = '\0';
				cnt++;
				cout << "Received: " << ibuf << endl;

				// задание 14: отправить сообщение без изменения обратно
				if (send(cS, ibuf, libuf, NULL) == SOCKET_ERROR)
					throw  SetErrorMsgText("send:", WSAGetLastError());
			}
			t2 = clock();

			cout << "\nClient disconnected. Messages: " << cnt << endl;
			// задание 14: время обмена сообщениями с клиентом
			cout << "Exchange time: " << ((double)(t2 - t1)) / CLOCKS_PER_SEC
				<< " seconds.\n" << endl;

			// задание 15: закрыть сокет клиента и ждать нового
			if (closesocket(cS) == SOCKET_ERROR)
				throw  SetErrorMsgText("closesocket:", WSAGetLastError());
		}

		// сюда не попадаем: сервер останавливается закрытием окна (Ctrl+C)
		if (closesocket(sS) == SOCKET_ERROR)
			throw  SetErrorMsgText("closesocket:", WSAGetLastError());
		if (WSACleanup() == SOCKET_ERROR) //завершение работы с библиотекой
			throw  SetErrorMsgText("Cleanup:", WSAGetLastError());
	}
	catch (string errorMsgText)
	{
		cout << endl << "WSAGetLastError: " << errorMsgText;
	}

	//................................................................
	return 0;
}
