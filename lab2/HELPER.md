# HELPER — разбор кода лабораторной работы № 2

> Шпаргалка для подготовки к защите. Здесь тот же код, что в `ServerT/` и
> `ClientT/`, но с подробными комментариями и объяснением каждой функции
> Winsock. В сами `.cpp`-файлы эти комментарии **не** переносятся — там
> только нумеровка заданий.

## 1. Общая схема работы (кто что вызывает и в каком порядке)

```
       ServerT (сервер)                          ClientT (клиент)
  ---------------------------              ---------------------------
  WSAStartup                                WSAStartup
       |                                         |
  socket  (слушающий listenSocket)          socket  (clientSocket)
       |                                         |
  bind    (привязка к порту 2000)                 |
       |                                         |
  listen  (очередь подключений)                   |
       |                                         |
  accept  --- БЛОКИРУЕТСЯ, ждёт клиента --- connect (стучится на IP:порт)
       |              TCP-соединение установлено  |
       |<------------- send("Hello ...") --------|
  recv  (принял)                            recv  (получил эхо)
       |--- send(то же самое обратно) ---------->|
       |              ... N сообщений ...        |
       |<------------- send("", 0) -------------|  (нулевое = конец)
  recv вернул 0  ->  конец обмена             closesocket
       |                                         |
  closesocket(clientSocket)                 WSACleanup
  accept  (снова ждёт следующего клиента)
```

## 2. Переменные и типы (что есть что)

| Имя | Тип | Что это |
|---|---|---|
| `listenSocket` | `SOCKET` | Слушающий сокет сервера. Создаётся `socket()`, к нему привязывается порт. Через него НИКАКИХ данных не передаётся — он только принимает подключения |
| `clientSocket` | `SOCKET` | У сервера — сокет подключившегося клиента (возвращает `accept()`), через него идёт `recv`/`send`. У клиента — его собственный сокет для связи с сервером |
| `wsaData` | `WSADATA` | Структура, которую заполняет `WSAStartup()` — сведения о версии библиотеки Winsock |
| `buffer` | `char[50]` | Буфер приёма сервера: сюда `recv()` записывает принятые байты |
| `receiveBuffer` / `sendBuffer` | `char[50]` | Буферы клиента: приём эха и отправка «Hello from Client xxx» |
| `receivedBytes` | `int` | Сколько байт реально вернул `recv()` |
| `messageCount` | `int` | Сервер: счётчик принятых сообщений |
| `messageTotal` | `int` | Клиент: сколько сообщений отправить (вводится с клавиатуры, задание 14) |
| `messageNumber` | `int` | Клиент: номер сообщения в тексте `Hello from Client 001` |
| `startTime`, `endTime` | `clock_t` | Метки времени до и после обмена; `(endTime - startTime) / CLOCKS_PER_SEC` — секунды |
| `serverAddress` | `SOCKADDR_IN` | Структура «семейство + порт + IP». У сервера — «за кем закрепить порт», у клиента — «к кому подключаться» |
| `clientAddress` | `SOCKADDR_IN` | Адрес подключившегося клиента, заполняется `accept()` |
| `clientAddressSize` | `int` | Размер структуры `clientAddress` в байтах — `accept()` требует его по указателю |
| `port` / `serverPort` | `int` | Порт из аргументов командной строки (по умолчанию 2000) |
| `serverIp` | `const char*` | IP-адрес сервера из аргументов (по умолчанию `127.0.0.1` — внутренняя петля) |

## 3. Аннотированный код сервера (ServerT_lab2.cpp)

```cpp
// Лабораторная работа № 2. Обмен данными по TCP-соединению
// Серверная часть (ServerT). Задания 1-6, 10, 11, 14, 15
#include <string>     // string, to_string — тексты ошибок
#include <iostream>   // cout, endl — вывод в консоль
#include <cstdlib>    // atoi — строку аргумента превращает в число
#include <ctime>      // clock_t, clock(), CLOCKS_PER_SEC — замер времени
#include <windows.h>  // базовые определения Windows (нужен MinGW, MSVC тянет сам)
#include "Winsock2.h" // все функции работы с сокетами
// Задание 1
#pragma comment(lib, "WS2_32.lib") // для MSVC: подключить библиотеку WS2_32
                     // (для g++ библиотека подключается ключом -lws2_32,
                     //  а эту директиву компилятор просто игнорирует)
using namespace std;

// Задание 2
// Принимает ЧИСЛОВОЙ код ошибки (от WSAGetLastError()) и возвращает
// ЧЕЛОВЕЧЕСКИЙ текст. switch перебирает коды, которые реально бывают
// у сокетных операций; полный список кодов есть в практикуме.
string GetErrorMsgText(int code)
{
	switch (code)
	{
	case WSAEINTR:          return "WSAEINTR: Работа функции прервана";
	// ...ещё полтора десятка кодов: WSAEACCES, WSAEADDRINUSE,
	// WSAECONNREFUSED, WSANOTINITIALISED и т. д. — все одинаково:
	// константа кода -> её текст
	default:                return "Ошибка Winsock, код " + to_string(code);
	// неизвестный код — печатаем хотя бы его номер
	}
}

// Задание 2
// Обёртка: склеивает место ошибки ("bind:", "recv:") и расшифровку кода.
// Результат — строка вида "bind:WSAEADDRINUSE: Адрес уже используется".
string SetErrorMsgText(string msgText, int code)
{
	return msgText + GetErrorMsgText(code);
}

int main(int argc, char* argv[])  // argc/argv — аргументы командной строки
{
	// Задание 5
	// Порт: первый аргумент запуска (ServerT.exe 3000) или 2000 по умолчанию.
	// atoi("3000") -> 3000.
	int port = (argc > 1) ? atoi(argv[1]) : 2000;

	SOCKET listenSocket;       // слушающий сокет — только принимает подключения
	SOCKET clientSocket;       // появится из accept — канал к конкретному клиенту
	WSADATA wsaData;           // сюда WSAStartup запишет инфо о библиотеке
	char buffer[50];           // recv пишет сюда принятые байты
	int receivedBytes;         // сколько байт реально принято за один recv
	int messageCount;          // счётчик принятых сообщений
	clock_t startTime, endTime; // начало и конец обмена (для замера времени)

	try  // любая ошибка ниже кидает throw string — ловим в catch внизу
	{
		// Задание 3
		// Инициализация Winsock. MAKEWORD(2,2) = "хочу версию 2.2".
		// Возвращает 0 при успехе, иначе — код ошибки.
		if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
			throw SetErrorMsgText("Startup:", WSAGetLastError());

		// Задание 4
		// Создаём потоковый (TCP) сокет:
		//   AF_INET     — адресация IPv4;
		//   SOCK_STREAM — потоковый сокет = TCP (SOCK_DGRAM был бы UDP);
		//   0           — протокол по умолчанию для этой пары (TCP).
		// При ошибке вернётся INVALID_SOCKET.
		if ((listenSocket = socket(AF_INET, SOCK_STREAM, 0)) == INVALID_SOCKET)
			throw SetErrorMsgText("socket:", WSAGetLastError());

		// Задание 5
		// Заполняем адрес, который займёт сервер:
		SOCKADDR_IN serverAddress;              // "семейство+порт+IP"
		serverAddress.sin_family = AF_INET;     // IPv4
		serverAddress.sin_port = htons(port);   // порт. htons = host-to-network
		                                        // short: переводит порядок байт
		                                        // (у x86 младший байт вперёд, а
		                                        //  в сети — старший). ЗАБЫТЬ htons
		                                        //  = порт 2000 превратится в 51200
		serverAddress.sin_addr.s_addr = INADDR_ANY; // слушать на ВСЕХ своих IP
	                                                // (и 127.0.0.1, и 192.168.x.x)

		// Привязываем сокет к адресу и порту: "этот сокет владеет портом".
		if (bind(listenSocket, (sockaddr*)&serverAddress, sizeof(serverAddress)) == SOCKET_ERROR)
			throw SetErrorMsgText("bind:", WSAGetLastError());

		// Переводим сокет в режим прослушивания: готов принимать
		// подключения. SOMAXCONN — максимальная очередь подключений.
		if (listen(listenSocket, SOMAXCONN) == SOCKET_ERROR)
			throw SetErrorMsgText("listen:", WSAGetLastError());

		cout << "ServerT: waiting for clients on port " << port << "..." << endl;

		// Задание 15
		// Вечный цикл: обслуживаем клиентов одного за другим.
		while (true)
		{
			SOCKADDR_IN clientAddress;             // accept запишет сюда адрес
			int clientAddressSize = sizeof(clientAddress); // размер структуры

			// Задание 6
			// accept БЛОКИРУЕТ программу: сервер "зависает" здесь и ждёт,
			// пока кто-нибудь не сделает connect. Когда клиент подключился,
			// accept создаёт НОВЫЙ сокет clientSocket — персональный канал
			// связи именно с этим клиентом. Слушающий listenSocket
			// продолжает ждать других.
			clientSocket = accept(listenSocket, (sockaddr*)&clientAddress, &clientAddressSize);
			if (clientSocket == INVALID_SOCKET)
				throw SetErrorMsgText("accept:", WSAGetLastError());

			// Задание 10
			// Печатаем, кто пришёл. inet_ntoa — IP из двоичного вида в
			// текст "127.0.0.1"; ntohs — порт из сетевого порядка байт
			// обратно в обычный (противоположность htons).
			cout << "\nClient connected." << endl;
			cout << "Client IP:    " << inet_ntoa(clientAddress.sin_addr) << endl;
			cout << "Client port:  " << ntohs(clientAddress.sin_port) << "\n\n";

			// Задания 11, 14
			messageCount = 0;
			startTime = clock();  // стартовая метка времени
			while (true)          // обмен с одним клиентом
			{
				// Задание 11
				// recv читает данные ИЗ СОКЕТА КЛИЕНТА (не из слушающего!).
				// Записывает в buffer не больше sizeof(buffer)=50 байт,
				// возвращает число реально принятых байт:
				//   > 0          — приняли receivedBytes байт;
				//   0            — клиент закрыл соединение (или прислал
				//                  сообщение нулевой длины) — конец обмена;
				//   SOCKET_ERROR — ошибка.
				receivedBytes = recv(clientSocket, buffer, sizeof(buffer), 0);
				if (receivedBytes == SOCKET_ERROR)
					throw SetErrorMsgText("recv:", WSAGetLastError());

				// Задание 14
				if (receivedBytes == 0)   // сообщение нулевой длины
					break;                // выходим из цикла обмена

				buffer[receivedBytes] = '\0'; // вручную ставим конец строки:
				                              // recv НЕ добавляет '\0', без
				                              // этого cout напечатает мусор
				messageCount++;               // ещё одно сообщение
				cout << "Received: " << buffer << endl;

				// Задание 14
				// Эхо: отправляем клиенту ровно то, что приняли,
				// receivedBytes байт (не strlen — честно возвращаем байты).
				if (send(clientSocket, buffer, receivedBytes, 0) == SOCKET_ERROR)
					throw SetErrorMsgText("send:", WSAGetLastError());
			}
			endTime = clock();  // конечная метка

			cout << "\nClient disconnected. Messages: " << messageCount << endl;
			// endTime-startTime — "тики" процессора; делим на
			// CLOCKS_PER_SEC -> секунды
			cout << "Exchange time: " << ((double)(endTime - startTime)) / CLOCKS_PER_SEC
				<< " seconds.\n" << endl;

			// Задание 15
			// Закрываем сокет ЭТОГО клиента. Слушающий listenSocket жив,
			// цикл идёт на новый accept — сервер готов к следующему.
			if (closesocket(clientSocket) == SOCKET_ERROR)
				throw SetErrorMsgText("closesocket:", WSAGetLastError());
		}

		// Сюда не попадём (while(true) бесконечный) — сервер останавливают
		// закрытием окна консоли. Написано для полноты схемы Winsock:
		if (closesocket(listenSocket) == SOCKET_ERROR)
			throw SetErrorMsgText("closesocket:", WSAGetLastError());
		if (WSACleanup() == SOCKET_ERROR)  // освобождаем ресурсы Winsock
			throw SetErrorMsgText("Cleanup:", WSAGetLastError());
	}
	catch (string errorMsgText)  // ловим строку из throw
	{
		cout << endl << "WSAGetLastError: " << errorMsgText;
	}

	return 0;
}
```

## 4. Аннотированный код клиента (ClientT_lab2.cpp)

```cpp
// Лабораторная работа № 2. Обмен данными по TCP-соединению
// Клиентская часть (ClientT). Задания 7-9, 11-14
#include <string>     // string (для сообщений об ошибках)
#include <iostream>   // cout, cin
#include <cstdio>     // sprintf — форматирование строки в буфер
#include <cstring>    // strlen — длина строки
#include <cstdlib>    // atoi
#include <ctime>      // clock
#include <windows.h>  // см. сервер
#include "Winsock2.h"
#pragma comment(lib, "WS2_32.lib")
using namespace std;

// Задание 7: GetErrorMsgText и SetErrorMsgText — точно такие же,
// как в сервере (задание 7 = повторить в клиенте задания 2-4).

int main(int argc, char* argv[])
{
	// Задание 12
	// Конфигурация подключения из командной строки:
	//   ClientT.exe                      -> 127.0.0.1:2000 (внутренняя петля)
	//   ClientT.exe 192.168.31.80 2000   -> явно заданные IP и порт
	// argc > 1 значит "есть хотя бы один аргумент" (argv[0] — имя программы)
	const char* serverIp = (argc > 1) ? argv[1] : "127.0.0.1";
	// Задание 12
	int serverPort = (argc > 2) ? atoi(argv[2]) : 2000;

	SOCKET clientSocket;         // сокет клиента
	WSADATA wsaData;
	char receiveBuffer[50];      // сюда приходит эхо от сервера
	char sendBuffer[50];         // отсюда уходит "Hello from Client 001"
	int receivedBytes;           // сколько байт приняли
	int messageTotal;            // сколько сообщений отправить (с клавиатуры)
	int messageNumber = 1;       // номер сообщения, начинается с 1
	clock_t startTime, endTime;  // замер времени обмена

	try
	{
		// Задание 7 (повтор задания 3)
		if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
			throw SetErrorMsgText("Startup:", WSAGetLastError());

		// Задание 7 (повтор задания 4)
		if ((clientSocket = socket(AF_INET, SOCK_STREAM, 0)) == INVALID_SOCKET)
			throw SetErrorMsgText("socket:", WSAGetLastError());

		// Задание 8
		// Адрес, КУДА подключаемся. Заполняем так же, как сервер свой,
		// но вместо INADDR_ANY — конкретный IP сервера.
		SOCKADDR_IN serverAddress;
		serverAddress.sin_family = AF_INET;
		serverAddress.sin_port = htons(serverPort); // тот же порт, что у сервера
		// Задание 12
		// inet_addr("192.168.31.80") превращает текстовый IP в 4-байтовое
		// число в сетевом порядке байт. 127.0.0.1 — внутренняя петля:
		// "сам на себя", ни один пакет не выходит в сеть.
		serverAddress.sin_addr.s_addr = inet_addr(serverIp);

		// Задание 8
		// Установка соединения. Если сервер не запущен, получим ошибку
		// WSAECONNREFUSED (код 10061) — "соединение отклонено": на этом
		// порту никто не слушает.
		if (connect(clientSocket, (sockaddr*)&serverAddress, sizeof(serverAddress)) == SOCKET_ERROR)
			throw SetErrorMsgText("connect:", WSAGetLastError());

		// Задание 14
		cout << "Enter number of messages: ";
		cin >> messageTotal;   // сколько сообщений отправить (например, 1000)

		// Задания 13, 14
		startTime = clock();
		for (int i = 0; i < messageTotal; i++)  // отправляем messageTotal сообщений
		{
			// Собираем текст "Hello from Client 001", "Hello from Client 002"...
			// %03d — число минимум из 3 цифр с ведущими нулями (001, 023, 100)
			sprintf(sendBuffer, "Hello from Client %03d", messageNumber);

			// Задание 11
			// Отправляем strlen+1 байт: +1 — чтобы ушёл и завершающий '\0'
			// (сервер его получит и не будет читать мусор).
			if (send(clientSocket, sendBuffer, strlen(sendBuffer) + 1, 0) == SOCKET_ERROR)
				throw SetErrorMsgText("send:", WSAGetLastError());

			// Задание 14
			// Ждём эхо: сервер обязан вернуть то же сообщение.
			receivedBytes = recv(clientSocket, receiveBuffer, sizeof(receiveBuffer), 0);
			if (receivedBytes == SOCKET_ERROR)
				throw SetErrorMsgText("recv:", WSAGetLastError());

			receiveBuffer[receivedBytes] = '\0';  // завершаем строку вручную
			cout << "Received: " << receiveBuffer << endl;

			// Задание 14
			messageNumber++;   // следующий номер сообщения
		}
		endTime = clock();

		// Задание 14
		cout << "\nExchange time for " << messageTotal << " messages: "
			<< ((double)(endTime - startTime)) / CLOCKS_PER_SEC << " seconds.\n";

		// Задание 14
		// Сообщение НУЛЕВОЙ длины — сигнал серверу "всё, конец обмена".
		// send("", 0, 0) отправляет 0 байт, на сервере recv вернёт 0.
		if (send(clientSocket, "", 0, 0) == SOCKET_ERROR)
			throw SetErrorMsgText("send:", WSAGetLastError());

		if (closesocket(clientSocket) == SOCKET_ERROR)  // закрыли сокет
			throw SetErrorMsgText("closesocket:", WSAGetLastError());
		if (WSACleanup() == SOCKET_ERROR)               // освободили Winsock
			throw SetErrorMsgText("Cleanup: ", WSAGetLastError());
	}
	catch (string errorMsgText)
	{
		cout << endl << "WSAGetLastError: " << errorMsgText;
	}

	return 0;
}
```

## 5. Справочник функций Winsock (что спросить на защите)

| Функция | Что делает | Возвращает |
|---|---|---|
| `WSAStartup(MAKEWORD(2,2), &wsaData)` | Инициализирует библиотеку Winsock, договариваемся о версии 2.2. Обязательна до любых других вызовов | `0` при успехе |
| `WSACleanup()` | Освобождает ресурсы Winsock. Вызывается в самом конце | `0` / `SOCKET_ERROR` |
| `socket(AF_INET, SOCK_STREAM, 0)` | Создаёт сокет: IPv4 + TCP | Дескриптор сокета или `INVALID_SOCKET` |
| `bind(listenSocket, &serverAddress, ...)` | Закрепляет сокет за портом (только сервер) | `0` / `SOCKET_ERROR` |
| `listen(listenSocket, SOMAXCONN)` | Включает приём подключений, задаёт размер очереди (только сервер) | `0` / `SOCKET_ERROR` |
| `accept(listenSocket, &clientAddress, &clientAddressSize)` | Блокируется до подключения клиента, создаёт новый сокет для общения с ним (только сервер) | Сокет клиента или `INVALID_SOCKET` |
| `connect(clientSocket, &serverAddress, ...)` | Подключается к серверу по IP и порту (только клиент) | `0` / `SOCKET_ERROR` |
| `send(socket, buffer, length, 0)` | Отправляет `length` байт из буфера | Число отправленных байт / `SOCKET_ERROR` |
| `recv(socket, buffer, length, 0)` | Принимает до `length` байт в буфер | Число байт / `0` (конец) / `SOCKET_ERROR` |
| `closesocket(socket)` | Закрывает сокет | `0` / `SOCKET_ERROR` |
| `WSAGetLastError()` | Код последней ошибки Winsock (его мы передаём в `GetErrorMsgText`) | Код ошибки |

Вспомогательные преобразования:

- `htons(port)` — **h**ost **to** **n**etwork **s**hort: порт из порядка байт
  машины в сетевой (big-endian). Обратно — `ntohs()`.
- `inet_addr("127.0.0.1")` — IP из текста в число. Обратно — `inet_ntoa()`.
- `INADDR_ANY` — «все мои IP-адреса»: сервер принимает подключения и по
  петле, и по сетевому IP.

## 6. Что показывать в netstat и почему

Во время обмена `netstat -a` показывает две строки на одно соединение:

```
TCP    127.0.0.1:2000     127.0.0.1:52134    ESTABLISHED   <- сокет сервера
TCP    127.0.0.1:52134    127.0.0.1:2000     ESTABLISHED   <- сокет клиента
```

- `2000` — порт, закреплённый `bind` за сервером;
- `52134` — динамический порт клиента (выдаётся системой из диапазона
  49152–65535, виден в `accept` через `ntohs(clientAddress.sin_port)`);
- `ESTABLISHED` — соединение установлено и живёт;
- до подключения у сервера строка `0.0.0.0:2000 ... LISTENING`;
- после разрыва соединение ещё ~2 минуты висит в `TIME_WAIT` — это
  норма TCP: система ждёт потерянные пакеты, прежде чем переиспользовать
  порт.

## 7. Сборка и запуск (обычный компилятор)

```
g++ -std=c++17 -o ServerT.exe ServerT/ServerT_lab2.cpp -lws2_32
g++ -std=c++17 -o ClientT.exe ClientT/ClientT_lab2.cpp -lws2_32
```

```
ServerT.exe [порт]
ClientT.exe [IP-адрес сервера] [порт]
```

По умолчанию: сервер слушает порт 2000, клиент идёт на 127.0.0.1:2000.
