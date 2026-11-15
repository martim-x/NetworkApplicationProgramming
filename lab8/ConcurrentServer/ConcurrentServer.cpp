#define _WINSOCK_DEPRECATED_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#include "../ServiceLibrary/ServiceLibrary.h"
#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <ctime>
#pragma comment(lib, "WS2_32.lib")
using namespace std;

struct ServerConfig
{
	int port;
	int responsePort;
	char dllName[260];
	char pipeName[260];
	char callSign[64];
};

static ServerConfig config;
static ListContact contacts[MAX_CONTACTS];
static StatsInfo stats;
static CRITICAL_SECTION contactLock;
static CRITICAL_SECTION consoleLock;
static CRITICAL_SECTION commandLock;
static HANDLE contactEvent = NULL;
static HANDLE acceptThreadHandle = NULL;
static char command[COMMAND_SIZE] = "start";
static volatile LONG accepting = 1;
static volatile LONG serverExit = 0;
static volatile LONG drainMode = 0;      // 0 — нет, 1 — wait, 2 — shutdown
static int serviceTimeoutMs = 60000;
static int lastPriorityLevel = -1;
static unsigned nextGeneration = 0;      // защищается contactLock
static FNSSS SSS = NULL;
static FNFindService FindService = NULL;

// Диагностика для удалённой консоли: ограниченная очередь событий сеансов,
// забирается служебной командой events (протокол ConsolePipe/RConsole).
static CRITICAL_SECTION eventsLock;
static vector<string> eventQueue;
static const size_t EVENT_QUEUE_LIMIT = 256;

static void AddEvent(const string& text)
{
	EnterCriticalSection(&eventsLock);

	if (eventQueue.size() >= EVENT_QUEUE_LIMIT)
		eventQueue.erase(eventQueue.begin());

	eventQueue.push_back(text);
	LeaveCriticalSection(&eventsLock);
}

static string GetErrorMsgText(int code)
{
	switch (code)
	{
	case WSAEINTR:              return "WSAEINTR: Работа функции прервана";
	case WSAEACCES:             return "WSAEACCES: Разрешение отвергнуто";
	case WSAEFAULT:             return "WSAEFAULT: Ошибочный адрес";
	case WSAEINVAL:             return "WSAEINVAL: Ошибка в аргументе";
	case WSAEMFILE:             return "WSAEMFILE: Слишком много файлов открыто";
	case WSAEWOULDBLOCK:        return "WSAEWOULDBLOCK: Ресурс временно недоступен";
	case WSAENOTSOCK:           return "WSAENOTSOCK: Сокет задан неправильно";
	case WSAEMSGSIZE:           return "WSAEMSGSIZE: Сообщение слишком длинное";
	case WSAEADDRINUSE:         return "WSAEADDRINUSE: Адрес уже используется";
	case WSAENETDOWN:           return "WSAENETDOWN: Сеть отключена";
	case WSAENETRESET:          return "WSAENETRESET: Сеть разорвала соединение";
	case WSAECONNABORTED:       return "WSAECONNABORTED: Программный отказ связи";
	case WSAECONNRESET:         return "WSAECONNRESET: Связь восстановлена";
	case WSAETIMEDOUT:          return "WSAETIMEDOUT: Закончился отведенный интервал времени";
	case WSAECONNREFUSED:       return "WSAECONNREFUSED: Соединение отклонено";
	case WSANOTINITIALISED:     return "WSANOTINITIALISED: Не выполнена инициализация WS2_32.DLL";
	case ERROR_FILE_NOT_FOUND:  return "ERROR_FILE_NOT_FOUND: Файл не найден";
	case ERROR_PIPE_BUSY:       return "ERROR_PIPE_BUSY: Все экземпляры канала заняты";
	case ERROR_BROKEN_PIPE:     return "ERROR_BROKEN_PIPE: Канал разорван";
	case ERROR_NO_DATA:         return "ERROR_NO_DATA: Канал закрывается";
	case ERROR_PIPE_CONNECTED:  return "ERROR_PIPE_CONNECTED: Есть процесс на другом конце канала";
	default:                    return "Ошибка, код " + to_string(code);
	}
}

static void Log(const string& text)
{
	EnterCriticalSection(&consoleLock);
	cout << text << endl;
	LeaveCriticalSection(&consoleLock);
}

static string EndpointToString(const SOCKADDR_IN& address)
{
	return string(inet_ntoa(address.sin_addr)) + ":" + to_string(ntohs(address.sin_port));
}

static void CopyEndpoint(char* dst, size_t size, const SOCKADDR_IN& address)
{
	string text = EndpointToString(address);

	if (text.size() >= size)
		text.resize(size - 1);

	memcpy(dst, text.c_str(), text.size());
	dst[text.size()] = '\0';
}

static void PrintStats()
{
	Log("--- Статистика сервера ---");
	Log("  подключений всего:  " + to_string(stats.totalConnections));
	Log("  активных сейчас:    " + to_string(stats.activeConnections));
	Log("  обслужено успешно:  " + to_string(stats.completedConnections));
	Log("  отказов:            " + to_string(stats.refusedConnections));
}

// Одна строка счётчиков для удалённой консоли (команда statistics по каналу)
static string StatsLine()
{
	return "total=" + to_string(stats.totalConnections) +
		" active=" + to_string(stats.activeConnections) +
		" completed=" + to_string(stats.completedConnections) +
		" refused=" + to_string(stats.refusedConnections);
}

static bool TakeCommand(string& result)
{
	bool hasCommand = false;

	EnterCriticalSection(&commandLock);
	if (strcmp(command, CMD_GET_COMMAND) != 0)
	{
		result = command;
		strcpy_s(command, COMMAND_SIZE, CMD_GET_COMMAND);
		hasCommand = true;
	}
	LeaveCriticalSection(&commandLock);

	return hasCommand;
}

static bool WaitForCommandTaken()
{
	for (int i = 0; i < 300; i++)
	{
		bool taken = false;

		EnterCriticalSection(&commandLock);
		taken = (strcmp(command, CMD_GET_COMMAND) == 0);
		LeaveCriticalSection(&commandLock);

		if (taken)
			return true;

		Sleep(10);
	}

	return false;
}

static bool HasPendingContacts()
{
	bool pending = false;

	EnterCriticalSection(&contactLock);

	for (int i = 0; i < MAX_CONTACTS; i++)
	{
		// слот не освобождён, пока жив обслуживающий поток: классификация
		// (completed/refused) выполняется в ReleaseSession, поэтому после
		// exit циклы завершаются только после освобождения всех слотов
		if (contacts[i].socket != INVALID_SOCKET || contacts[i].serviceThread != NULL)
		{
			pending = true;
			break;
		}
	}

	LeaveCriticalSection(&contactLock);
	return pending;
}

// Аварийное закрытие сокета сеанса: shutdown() не прерывает блокирующий recv
// обслуживающего потока, поэтому используется SO_LINGER=0 + closesocket (RST).
// Pending recv возвращается с ошибкой, поток завершается штатно.
static void AbortSessionSocket(ListContact& contact)
{
    if (contact.socket == INVALID_SOCKET)
        return;

    LINGER linger;
    linger.l_onoff = 1;
    linger.l_linger = 0;
    setsockopt(contact.socket, SOL_SOCKET, SO_LINGER,
        (const char*)&linger, sizeof(linger));

    closesocket(contact.socket);
    contact.socket = INVALID_SOCKET;
}

static void InterruptActiveSessions()
{
	EnterCriticalSection(&contactLock);

	for (int i = 0; i < MAX_CONTACTS; i++)
	{
		if (contacts[i].socket != INVALID_SOCKET)
			AbortSessionSocket(contacts[i]);
	}

	LeaveCriticalSection(&contactLock);
}

static void ProcessCommand()
{
	string cmd;
	if (!TakeCommand(cmd))
		return;

	if (cmd == "start")
	{
		InterlockedExchange(&accepting, 1);
		InterlockedExchange(&drainMode, 0);
		Log("AcceptServer: команда start — приём клиентов разрешён");
	}
	else if (cmd == "stop")
	{
		InterlockedExchange(&accepting, 0);
		Log("AcceptServer: команда stop — приём клиентов запрещён");
	}
	else if (cmd == "statistics")
	{
		PrintStats();
	}
	else if (cmd == "wait")
	{
		InterlockedExchange(&accepting, 0);
		InterlockedExchange(&drainMode, 1);
		Log("AcceptServer: команда wait — ожидание завершения обслуживания клиентов");
	}
	else if (cmd == "shutdown")
	{
		InterlockedExchange(&accepting, 0);
		InterlockedExchange(&drainMode, 2);
		Log("AcceptServer: команда shutdown — приём запрещён, ждём обслуживания");
	}
	else if (cmd == "exit")
	{
		InterlockedExchange(&accepting, 0);
		InterruptActiveSessions();
		InterlockedExchange(&serverExit, 1);
		Log("AcceptServer: сервер завершает работу (exit)");
	}
	else
	{
		Log("AcceptServer: неизвестная команда \"" + cmd + "\"");
	}
}

// Снимок данных сеанса для APC-уведомлений: отложенный APC не читает поля слота,
// поэтому повторное использование слота не искажает отчёт о чужом сеансе.
struct ServiceNotice
{
	char endpoint[64];
	int messageCount;
	double seconds;
	bool started;
};

static VOID CALLBACK ServiceNoticeAPC(ULONG_PTR param)
{
	ServiceNotice* notice = (ServiceNotice*)param;

	if (notice == NULL)
		return;

	if (notice->started)
	{
		Log(string("APC AcceptServer: старт обслуживания клиента ") + notice->endpoint);
	}
	else
	{
		char buffer[128];
		sprintf_s(buffer, sizeof(buffer), "%.3f", notice->seconds);
		Log(string("APC AcceptServer: завершение обслуживания клиента ") + notice->endpoint +
			", сообщений " + to_string(notice->messageCount) + ", время " + buffer + " с");
	}

	delete notice;
}

static ULONG_PTR MakeSessionToken(int index, unsigned generation)
{
	return ((ULONG_PTR)generation << 16) | (ULONG_PTR)(index & 0xFFFF);
}

// APC ожидающего таймера сеанса: выполняется в alertable-цикле AcceptServer.
// Владение слотом проверяется по поколению под contactLock, поэтому отложенный
// срабатывающий таймер не может завершить чужой сеанс.
static VOID CALLBACK SessionTimerAPC(LPVOID param, DWORD, DWORD)
{
	ULONG_PTR token = (ULONG_PTR)param;
	int index = (int)(token & 0xFFFF);
	unsigned generation = (unsigned)(token >> 16);

	if (index < 0 || index >= MAX_CONTACTS)
		return;

	EnterCriticalSection(&contactLock);
	ListContact& contact = contacts[index];

	if (contact.generation != generation || contact.socket == INVALID_SOCKET || contact.finished)
	{
		LeaveCriticalSection(&contactLock);
		return;
	}

	InterlockedExchange(&contact.failed, 1);
	InterlockedExchange(&contact.finished, 1);
	AbortSessionSocket(contact);

	Log("Watchdog: клиент " + EndpointToString(contact.address) +
		" превысил лимит " + to_string(serviceTimeoutMs) +
		" мс — сокет закрыт, обслуживающий поток завершится штатно");

	AddEvent("таймаут: клиент " + EndpointToString(contact.address) +
		" превысил лимит " + to_string(serviceTimeoutMs) + " мс");

	LeaveCriticalSection(&contactLock);
}

static ListContact* AddContact(SOCKET client, const SOCKADDR_IN& address)
{
	ListContact* result = NULL;

	EnterCriticalSection(&contactLock);

	for (int i = 0; i < MAX_CONTACTS; i++)
	{
		if (contacts[i].socket != INVALID_SOCKET)
			continue;

		ListContact& contact = contacts[i];
		contact.socket = client;
		contact.address = address;
		contact.serviceThread = NULL;
		contact.acceptThread = acceptThreadHandle;
		contact.watchdog = NULL;
		contact.busy = 0;
		contact.finished = 0;
		contact.failed = 0;
		contact.startedAt = GetTickCount();
		contact.messageCount = 0;
		contact.pendingBytes = 0;
		contact.incomingBytes = 0;
		memset(contact.pending, 0, sizeof(contact.pending));
		memset(contact.incoming, 0, sizeof(contact.incoming));
		memset(contact.request, 0, sizeof(contact.request));
		contact.generation = ++nextGeneration;

		// Ожидающий таймер сеанса взводится сразу после accept: он охватывает
		// и клиента, который ещё не отправил запрос, и само обслуживание.
		HANDLE timer = CreateWaitableTimerA(NULL, FALSE, NULL);

		if (timer != NULL)
		{
			LARGE_INTEGER dueTime;
			dueTime.QuadPart = -((LONGLONG)serviceTimeoutMs * 10000);

			if (SetWaitableTimer(timer, &dueTime, 0, SessionTimerAPC,
					(LPVOID)MakeSessionToken(i, contact.generation), FALSE))
			{
				contact.watchdog = timer;
			}
			else
			{
				CloseHandle(timer);
			}
		}

		result = &contact;
		break;
	}

	LeaveCriticalSection(&contactLock);
	return result;
}

static void ReleaseSession(ListContact& contact)
{
	// Обслуживающий поток обязан завершиться до освобождения слота: иначе
	// слот может быть переиспользован, а поток продолжит работать с ним.
	if (contact.serviceThread != NULL)
	{
		if (WaitForSingleObject(contact.serviceThread, serverExit ? 5000 : 0) != WAIT_OBJECT_0)
			return;

		CloseHandle(contact.serviceThread);
		contact.serviceThread = NULL;
	}

	if (contact.watchdog != NULL)
	{
		CancelWaitableTimer(contact.watchdog);
		CloseHandle(contact.watchdog);
		contact.watchdog = NULL;
	}

	ServiceNotice* notice = new ServiceNotice();
	CopyEndpoint(notice->endpoint, sizeof(notice->endpoint), contact.address);
	notice->messageCount = contact.messageCount;
	notice->seconds = (GetTickCount() - contact.startedAt) / 1000.0;
	notice->started = false;

	if (acceptThreadHandle != NULL)
		QueueUserAPC(ServiceNoticeAPC, acceptThreadHandle, (ULONG_PTR)notice);

	AddEvent(string(contact.failed ? "отказ" : "завершён") +
		": клиент " + notice->endpoint +
		", сообщений " + to_string(contact.messageCount) +
		", время " + to_string((int)(notice->seconds * 1000 + 0.5) / 1000.0) + " с");

	if (contact.socket != INVALID_SOCKET)
	{
		closesocket(contact.socket);
		contact.socket = INVALID_SOCKET;
	}
	contact.busy = 0;
	contact.finished = 0;

	InterlockedDecrement(&stats.activeConnections);

	// ровно одна терминальная классификация на соединение
	if (contact.failed)
		InterlockedIncrement(&stats.refusedConnections);
	else
		InterlockedIncrement(&stats.completedConnections);
}

static DWORD WINAPI AcceptServer(LPVOID)
{
	SOCKET listenSocket;
	SOCKADDR_IN serverAddress;

	if ((listenSocket = socket(AF_INET, SOCK_STREAM, 0)) == INVALID_SOCKET)
	{
		Log("AcceptServer: socket: " + GetErrorMsgText(WSAGetLastError()));
		return 1;
	}

	memset(&serverAddress, 0, sizeof(serverAddress));
	serverAddress.sin_family = AF_INET;
	serverAddress.sin_port = htons((short)config.port);
	serverAddress.sin_addr.s_addr = INADDR_ANY;

	if (bind(listenSocket, (LPSOCKADDR)&serverAddress, sizeof(serverAddress)) == SOCKET_ERROR)
	{
		Log("AcceptServer: bind: " + GetErrorMsgText(WSAGetLastError()));
		closesocket(listenSocket);
		return 1;
	}

	if (listen(listenSocket, SOMAXCONN) == SOCKET_ERROR)
	{
		Log("AcceptServer: listen: " + GetErrorMsgText(WSAGetLastError()));
		closesocket(listenSocket);
		return 1;
	}

	u_long nonBlocking = 1;
	ioctlsocket(listenSocket, FIONBIO, &nonBlocking);

	Log("AcceptServer: прослушивание TCP-порта " + to_string(config.port));

	while (!serverExit || HasPendingContacts())
	{
		SleepEx(0, TRUE);
		ProcessCommand();

		// wait/shutdown не блокируют цикл: команды продолжают обрабатываться,
		// пока существующие клиенты завершают обслуживание
		int mode = drainMode;

		if (mode != 0 && (stats.activeConnections == 0 || serverExit))
		{
			if (InterlockedCompareExchange(&drainMode, 0, mode) == mode)
			{
				if (mode == 1)
				{
					if (!serverExit)
						Log("AcceptServer: все подключённые клиенты обслужены");
				}
				else
				{
					Log("AcceptServer: сервер завершает работу (shutdown)");
					InterlockedExchange(&serverExit, 1);
				}
			}
		}

		if (!accepting)
		{
			Sleep(20);
			continue;
		}

		SOCKADDR_IN clientAddress;
		int clientLength = sizeof(clientAddress);
		memset(&clientAddress, 0, sizeof(clientAddress));

		SOCKET client = accept(listenSocket, (sockaddr*)&clientAddress, &clientLength);

		if (client == INVALID_SOCKET)
		{
			int err = WSAGetLastError();
			if (err != WSAEWOULDBLOCK && err != WSAEINTR)
				Log("AcceptServer: accept: " + GetErrorMsgText(err));
			Sleep(10);
			continue;
		}

		ListContact* contact = AddContact(client, clientAddress);

		if (contact == NULL)
		{
			send(client, MSG_ERROR_INQUIRY, (int)strlen(MSG_ERROR_INQUIRY) + 1, 0);
			closesocket(client);
			InterlockedIncrement(&stats.refusedConnections);
			Log("AcceptServer: нет свободных мест в списке подключений, клиент отклонён");
			continue;
		}

		InterlockedIncrement(&stats.totalConnections);
			InterlockedIncrement(&stats.activeConnections);

			Log("AcceptServer: подключён клиент " + EndpointToString(clientAddress));
			AddEvent("подключён: клиент " + EndpointToString(clientAddress));
			SetEvent(contactEvent);
	}

	closesocket(listenSocket);
	Log("AcceptServer: завершён");
	return 0;
}

static void RefuseSession(ListContact& contact, const string& reason)
{
	if (contact.socket != INVALID_SOCKET)
		send(contact.socket, MSG_ERROR_INQUIRY, (int)strlen(MSG_ERROR_INQUIRY) + 1, 0);

	InterlockedExchange(&contact.failed, 1);
	InterlockedExchange(&contact.finished, 1);

	Log("DispatchServer: отказ \"" + reason + "\" от " +
		EndpointToString(contact.address) + ", отправлен ErrorInquiry");

	AddEvent("отказ: клиент " + EndpointToString(contact.address) +
		", причина: " + reason);
}

static DWORD WINAPI DispatchServer(LPVOID)
{
	Log("DispatchServer: запущен");

	while (!serverExit)
	{
		WaitForSingleObject(contactEvent, 50);

		EnterCriticalSection(&contactLock);

		for (int i = 0; i < MAX_CONTACTS; i++)
		{
			ListContact& contact = contacts[i];

			if (contact.socket == INVALID_SOCKET || contact.busy || contact.finished)
				continue;

			// первый запрос — NUL-терминированный элемент протокола;
			// накапливаем байты, пока терминатор не придёт (таймер сеанса
			// закроет соединение, если клиент молчит)
			int space = (int)sizeof(contact.incoming) - 1 - contact.incomingBytes;

			if (space <= 0)
			{
				RefuseSession(contact, "переполнение буфера первого запроса");
				continue;
			}

			int bytes = recv(contact.socket,
				contact.incoming + contact.incomingBytes, space, 0);

			if (bytes == SOCKET_ERROR)
			{
				int err = WSAGetLastError();

				if (err == WSAEWOULDBLOCK)
					continue;

				Log("DispatchServer: recv: " + GetErrorMsgText(err));
				InterlockedExchange(&contact.failed, 1);
				InterlockedExchange(&contact.finished, 1);
				continue;
			}

			if (bytes == 0)
			{
				Log("DispatchServer: клиент " + EndpointToString(contact.address) +
					" отключился без запроса");
				InterlockedExchange(&contact.failed, 1);
				InterlockedExchange(&contact.finished, 1);
				continue;
			}

			contact.incomingBytes += bytes;

			char* terminator = (char*)memchr(contact.incoming, 0,
				(size_t)contact.incomingBytes);

			if (terminator == NULL)
			{
				if (contact.incomingBytes >= (int)sizeof(contact.request))
					RefuseSession(contact, "первый запрос длиннее " +
						to_string(sizeof(contact.request) - 1) + " байт");

				continue;
			}

			int itemLength = (int)(terminator - contact.incoming);

			if (itemLength > (int)sizeof(contact.request) - 1)
			{
				RefuseSession(contact, "первый запрос длиннее " +
					to_string(sizeof(contact.request) - 1) + " байт");
				continue;
			}

			memcpy(contact.request, contact.incoming, (size_t)itemLength);
			contact.request[itemLength] = '\0';

			// байты payload после терминатора запроса сохраняются для обслуживающего потока
			contact.pendingBytes = contact.incomingBytes - itemLength - 1;

			if (contact.pendingBytes > 0)
				memcpy(contact.pending, terminator + 1, (size_t)contact.pendingBytes);
			else
				contact.pendingBytes = 0;

			FNService service = FindService(contact.request);

			if (service == NULL)
			{
				RefuseSession(contact, "неизвестный запрос \"" +
					string(contact.request) + "\"");
				continue;
			}

			u_long blocking = 0;
			ioctlsocket(contact.socket, FIONBIO, &blocking);

			InterlockedExchange(&contact.busy, 1);
			contact.messageCount = 1;

			Log("DispatchServer: запрос \"" + string(contact.request) + "\" от " +
				EndpointToString(contact.address) + " — запуск обслуживающего потока");

			ServiceRequest* request = new ServiceRequest();
			request->service = service;
			request->contact = &contact;

			if (SSS(request) != 0)
			{
				InterlockedExchange(&contact.failed, 1);
				InterlockedExchange(&contact.finished, 1);
				continue;
			}

			ServiceNotice* notice = new ServiceNotice();
			CopyEndpoint(notice->endpoint, sizeof(notice->endpoint), contact.address);
			notice->messageCount = 0;
			notice->seconds = 0.0;
			notice->started = true;

			if (acceptThreadHandle != NULL)
				QueueUserAPC(ServiceNoticeAPC, acceptThreadHandle, (ULONG_PTR)notice);
			else
				delete notice;
		}

		LeaveCriticalSection(&contactLock);
	}

	Log("DispatchServer: завершён");
	return 0;
}

static void UpdatePriorities()
{
	int active = stats.activeConnections;
	int level = (active > 2) ? 1 : 0;

	if (level == lastPriorityLevel)
		return;

	lastPriorityLevel = level;
	int priority = (level == 1) ? THREAD_PRIORITY_BELOW_NORMAL : THREAD_PRIORITY_NORMAL;

	EnterCriticalSection(&contactLock);

	for (int i = 0; i < MAX_CONTACTS; i++)
	{
		if (contacts[i].socket != INVALID_SOCKET && contacts[i].serviceThread != NULL)
			SetThreadPriority(contacts[i].serviceThread, priority);
	}

	LeaveCriticalSection(&contactLock);

	Log("Диспетчер приоритетов: активных сеансов " + to_string(active) +
		", приоритет обслуживающих потоков " + ((level == 1) ? "BELOW_NORMAL" : "NORMAL"));
}

static DWORD WINAPI GarbageCleaner(LPVOID)
{
	Log("GarbageCleaner: запущен");

	while (!serverExit || HasPendingContacts())
	{
		Sleep(100);

		if (!serverExit)
			UpdatePriorities();

		EnterCriticalSection(&contactLock);

		for (int i = 0; i < MAX_CONTACTS; i++)
		{
			ListContact& contact = contacts[i];

			// освобождаем любой завершённый сеанс; сокет может уже быть
			// аварийно закрыт таймаутом или exit (socket == INVALID_SOCKET)
			if (contact.socket == INVALID_SOCKET && !contact.finished)
				continue;

			if (!contact.finished)
				continue;

			ReleaseSession(contact);
		}

		LeaveCriticalSection(&contactLock);
	}

	Log("GarbageCleaner: завершён");
	return 0;
}

static DWORD WINAPI ConsolePipe(LPVOID)
{
	HANDLE hPipe;
	char buffer[COMMAND_SIZE];
	DWORD bytesRead, bytesWritten;

	// Очередь событий очищается только после успешной записи ответа: потеря
	// доставки не должна терять диагностику. Буфер канала рассчитан на
	// несколько событий сразу.
	if ((hPipe = CreateNamedPipeA(config.pipeName,
		PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
		PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
		1,
		4096,
		512,
		0,
		NULL)) == INVALID_HANDLE_VALUE)
	{
		Log("ConsolePipe: CreateNamedPipe: " + GetErrorMsgText(GetLastError()));
		return 1;
	}

	Log("ConsolePipe: канал управления создан: " + string(config.pipeName));

	OVERLAPPED connectOverlapped;
	memset(&connectOverlapped, 0, sizeof(connectOverlapped));
	HANDLE pipeEvent = CreateEventA(NULL, TRUE, FALSE, NULL);

	while (!serverExit)
	{
		if (pipeEvent == NULL)
			break;

		// overlapped-подключение с возможностью отмены при завершении сервера:
		// блокирующий ConnectNamedPipe не позволил бы потоку закончиться
		memset(&connectOverlapped, 0, sizeof(connectOverlapped));
		connectOverlapped.hEvent = pipeEvent;

		BOOL connected = ConnectNamedPipe(hPipe, &connectOverlapped);
		int connectErr = connected ? 0 : GetLastError();

		if (!connected && connectErr == ERROR_IO_PENDING)
		{
			// ждать подключения, оставаясь отзывчивым к команде завершения
			while (!serverExit)
			{
				if (WaitForSingleObject(pipeEvent, 200) == WAIT_OBJECT_0)
					break;
			}

			if (serverExit)
			{
				CancelIoEx(hPipe, &connectOverlapped);
				DWORD settle = 0;
				GetOverlappedResult(hPipe, &connectOverlapped, &settle, TRUE);
				break;
			}

			// уточнить фактический результат подключения
			DWORD settle = 0;
			BOOL got = GetOverlappedResult(hPipe, &connectOverlapped, &settle, FALSE);
			connectErr = got ? 0 : GetLastError();

			if (!got && connectErr == ERROR_PIPE_CONNECTED)
				got = TRUE;

			if (!got)
			{
				Log("ConsolePipe: ConnectNamedPipe: " + GetErrorMsgText(connectErr));
				Sleep(100);
				continue;
			}
		}
		else if (!connected && connectErr != ERROR_PIPE_CONNECTED)
		{
			Log("ConsolePipe: ConnectNamedPipe: " + GetErrorMsgText(connectErr));
			Sleep(100);
			continue;
		}

		// чтение команды: тоже overlapped, с отменой при завершении сервера
		memset(buffer, 0, sizeof(buffer));
		OVERLAPPED readOverlapped;
		memset(&readOverlapped, 0, sizeof(readOverlapped));
		readOverlapped.hEvent = pipeEvent;
		bytesRead = 0;

		BOOL readOk = ReadFile(hPipe, buffer, sizeof(buffer) - 1, &bytesRead, &readOverlapped);
		int readErr = readOk ? 0 : GetLastError();

		if (!readOk && readErr == ERROR_IO_PENDING)
		{
			while (!serverExit)
			{
				if (WaitForSingleObject(pipeEvent, 200) == WAIT_OBJECT_0)
					break;
			}

			if (serverExit)
			{
				CancelIoEx(hPipe, &readOverlapped);
				DWORD settle = 0;
				GetOverlappedResult(hPipe, &readOverlapped, &settle, TRUE);
				break;
			}

			DWORD settle = 0;
			readOk = GetOverlappedResult(hPipe, &readOverlapped, &settle, FALSE);
			bytesRead = settle;
			readErr = readOk ? 0 : GetLastError();
		}

		if (!readOk && readErr != ERROR_BROKEN_PIPE && readErr != ERROR_NO_DATA)
			Log("ConsolePipe: ReadFile: " + GetErrorMsgText(readErr));

		if (!readOk || bytesRead == 0)
		{
			DisconnectNamedPipe(hPipe);
			continue;
		}

		buffer[bytesRead] = '\0';

		string cmd = buffer;
		bool known = (cmd == "start" || cmd == "stop" || cmd == "exit" ||
			cmd == "statistics" || cmd == "wait" || cmd == "shutdown");
		bool servedLocally = (cmd == "statistics" || cmd == "events");

		if (!known && !servedLocally)
		{
			WriteFile(hPipe, MSG_NO_COMMAND, (DWORD)strlen(MSG_NO_COMMAND) + 1, &bytesWritten, NULL);
			FlushFileBuffers(hPipe);
			Log("ConsolePipe: неизвестная команда \"" + cmd + "\", отправлено nocmd");
		}
		else if (servedLocally)
		{
			// statistics/events отвечают в канал напрямую из ConsolePipe:
			// счётчики и диагностика не должны зависеть от очереди команд
			string answer;

			if (cmd == "statistics")
			{
				answer = StatsLine();
				Log("ConsolePipe: статистика отправлена в канал: " + answer);
			}
			else
			{
				EnterCriticalSection(&eventsLock);

				for (size_t i = 0; i < eventQueue.size(); i++)
				{
					if (!answer.empty())
						answer += "\n";

					answer += eventQueue[i];
				}

				LeaveCriticalSection(&eventsLock);

				Log("ConsolePipe: в канал отправлено событий: " +
					to_string(answer.empty() ? 0 : (int)count(answer.begin(), answer.end(), '\n') + 1));
			}

			if (answer.empty())
				answer = "-";

			BOOL delivered = WriteFile(hPipe, answer.c_str(), (DWORD)answer.size() + 1, &bytesWritten, NULL);

			// очередь очищается только после успешной доставки ответа
			if (cmd == "events" && delivered)
			{
				EnterCriticalSection(&eventsLock);
				eventQueue.clear();
				LeaveCriticalSection(&eventsLock);
			}

			FlushFileBuffers(hPipe);
		}
		else
		{
			EnterCriticalSection(&commandLock);
			strcpy_s(command, COMMAND_SIZE, cmd.c_str());
			LeaveCriticalSection(&commandLock);

			if (!WaitForCommandTaken())
				Log("ConsolePipe: поток-получатель не забрал команду \"" + cmd + "\"");

			WriteFile(hPipe, cmd.c_str(), (DWORD)cmd.size() + 1, &bytesWritten, NULL);
			FlushFileBuffers(hPipe);
			Log("ConsolePipe: принята команда \"" + cmd + "\"");
		}

		DisconnectNamedPipe(hPipe);
	}

	CloseHandle(hPipe);

	if (pipeEvent != NULL)
		CloseHandle(pipeEvent);

	Log("ConsolePipe: завершён");
	return 0;
}

static DWORD WINAPI ResponseServer(LPVOID)
{
	SOCKET s;
	SOCKADDR_IN address;

	if ((s = socket(AF_INET, SOCK_DGRAM, 0)) == INVALID_SOCKET)
	{
		Log("ResponseServer: socket: " + GetErrorMsgText(WSAGetLastError()));
		return 1;
	}

	int reuse = 1;
	setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));

	memset(&address, 0, sizeof(address));
	address.sin_family = AF_INET;
	address.sin_port = htons((short)config.responsePort);
	address.sin_addr.s_addr = INADDR_ANY;

	if (bind(s, (LPSOCKADDR)&address, sizeof(address)) == SOCKET_ERROR)
	{
		Log("ResponseServer: bind: " + GetErrorMsgText(WSAGetLastError()));
		closesocket(s);
		return 1;
	}

	u_long nonBlocking = 1;
	ioctlsocket(s, FIONBIO, &nonBlocking);

	Log("ResponseServer: приём широковещательных запросов на UDP-порт " +
		to_string(config.responsePort) + ", позывной \"" + string(config.callSign) + "\"");

	char buffer[64];

	while (!serverExit)
	{
		SOCKADDR_IN from;
		int fromLength = sizeof(from);
		memset(&from, 0, sizeof(from));

		int bytes = recvfrom(s, buffer, sizeof(buffer) - 1, 0, (sockaddr*)&from, &fromLength);

		if (bytes == SOCKET_ERROR)
		{
			int err = WSAGetLastError();
			if (err != WSAEWOULDBLOCK && err != WSAEINTR)
				Log("ResponseServer: recvfrom: " + GetErrorMsgText(err));
			Sleep(20);
			continue;
		}

		if (bytes == 0)
			continue;

		buffer[bytes] = '\0';

		if (strcmp(buffer, config.callSign) == 0)
		{
			sendto(s, config.callSign, (int)strlen(config.callSign) + 1, 0,
				(sockaddr*)&from, fromLength);
			Log("ResponseServer: отклик на запрос от " + EndpointToString(from));
		}
	}

	closesocket(s);
	Log("ResponseServer: завершён");
	return 0;
}

static void NormalizePipeName(char* name, size_t size)
{
	if (strncmp(name, "\\\\", 2) == 0)
		return;

	char shortName[260];
	strcpy_s(shortName, name);
	strcpy_s(name, size, "\\\\.\\pipe\\");
	strcat_s(name, size, shortName);
}

static FARPROC FindExport(HMODULE library, const char* name)
{
	FARPROC address = GetProcAddress(library, name);

	if (address != NULL)
		return address;

	char decorated[128];
	sprintf_s(decorated, "_%s@4", name);
	address = GetProcAddress(library, decorated);

	if (address != NULL)
		return address;

	sprintf_s(decorated, "%s@4", name);
	return GetProcAddress(library, decorated);
}

int main(int argc, char* argv[])
{
	SetConsoleOutputCP(CP_UTF8);

	config.port = (argc > 1) ? atoi(argv[1]) : 2000;
	strcpy_s(config.dllName, (argc > 2) ? argv[2] : "ServiceLibrary.dll");
	strcpy_s(config.pipeName, (argc > 3) ? argv[3] : "\\\\.\\pipe\\ConcurrentServer");
	config.responsePort = (argc > 4) ? atoi(argv[4]) : 9200;
	strcpy_s(config.callSign, (argc > 5) ? argv[5] : "ConcurrentServer");
	serviceTimeoutMs = (argc > 6) ? atoi(argv[6]) : 60000;

	NormalizePipeName(config.pipeName, sizeof(config.pipeName));

	if (strncmp(config.pipeName, "\\\\.\\", 3) != 0)
	{
		Log("main: сервер создаёт канал только на локальном компьютере, получено: " +
			string(config.pipeName));
		return 1;
	}

	WSADATA wsaData;
	HMODULE library = NULL;

	InitializeCriticalSection(&contactLock);
	InitializeCriticalSection(&consoleLock);
	InitializeCriticalSection(&commandLock);
	InitializeCriticalSection(&eventsLock);

	for (int i = 0; i < MAX_CONTACTS; i++)
	{
		memset(&contacts[i], 0, sizeof(contacts[i]));
		contacts[i].socket = INVALID_SOCKET;
	}

	contactEvent = CreateEvent(NULL, FALSE, FALSE, NULL);

	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
	{
		Log("main: Startup: " + GetErrorMsgText(WSAGetLastError()));
		return 1;
	}

	Log("ConcurrentServer: TCP-порт " + to_string(config.port) +
		", UDP-порт " + to_string(config.responsePort) +
		", канал управления " + string(config.pipeName) +
		", позывной \"" + string(config.callSign) + "\"");

	library = LoadLibraryA(config.dllName);
	if (library == NULL)
	{
		Log("main: LoadLibrary " + string(config.dllName) + ": " + GetErrorMsgText(GetLastError()));
		return 1;
	}

	SSS = (FNSSS)FindExport(library, "SSS");
	FindService = (FNFindService)FindExport(library, "FindService");

	if (SSS == NULL || FindService == NULL)
	{
		Log("main: в библиотеке не найдены экспортируемые функции SSS/FindService "
			"(проверьте разрядность DLL и EXE: x86-имена экспортируются как _SSS@4)");
		FreeLibrary(library);
		return 1;
	}

	Log("main: библиотека " + string(config.dllName) + " загружена, SSS и FindService импортированы");

	// проверки создания всех основных потоков сервера
	HANDLE dispatchThread = NULL;
	HANDLE garbageThread = NULL;
	HANDLE consoleThread = NULL;
	HANDLE responseThread = NULL;

	acceptThreadHandle = CreateThread(NULL, 0, AcceptServer, NULL, 0, NULL);
	if (acceptThreadHandle == NULL)
	{
		Log("main: не удалось создать поток AcceptServer");
		FreeLibrary(library);
		WSACleanup();
		return 1;
	}
	SetThreadPriority(acceptThreadHandle, THREAD_PRIORITY_ABOVE_NORMAL);

	if ((dispatchThread = CreateThread(NULL, 0, DispatchServer, NULL, 0, NULL)) == NULL ||
		(garbageThread = CreateThread(NULL, 0, GarbageCleaner, NULL, 0, NULL)) == NULL ||
		(consoleThread = CreateThread(NULL, 0, ConsolePipe, NULL, 0, NULL)) == NULL ||
		(responseThread = CreateThread(NULL, 0, ResponseServer, NULL, 0, NULL)) == NULL)
	{
		Log("main: не удалось создать один из основных потоков сервера");
		InterlockedExchange(&serverExit, 1);
	}

	while (!serverExit)
		Sleep(50);

	Log("main: ожидание завершения потоков сервера");

	// сборщик мусора освобождает все сеансы (дожидаясь обслуживающих потоков)
	WaitForSingleObject(garbageThread, 10000);
	WaitForSingleObject(acceptThreadHandle, 10000);
	WaitForSingleObject(dispatchThread, 5000);
	WaitForSingleObject(consoleThread, 5000);
	WaitForSingleObject(responseThread, 5000);

	// защитный проход: обслуживающие потоки завершаются до выгрузки DLL
	EnterCriticalSection(&contactLock);

	for (int i = 0; i < MAX_CONTACTS; i++)
	{
		ListContact& contact = contacts[i];

		if (contact.serviceThread != NULL)
		{
			WaitForSingleObject(contact.serviceThread, 5000);
			CloseHandle(contact.serviceThread);
			contact.serviceThread = NULL;
		}

		if (contact.watchdog != NULL)
		{
			CancelWaitableTimer(contact.watchdog);
			CloseHandle(contact.watchdog);
			contact.watchdog = NULL;
		}

		if (contact.socket != INVALID_SOCKET)
		{
			closesocket(contact.socket);
			contact.socket = INVALID_SOCKET;
		}
	}

	LeaveCriticalSection(&contactLock);

	CloseHandle(acceptThreadHandle);
	CloseHandle(dispatchThread);
	CloseHandle(garbageThread);
	CloseHandle(consoleThread);
	CloseHandle(responseThread);
	CloseHandle(contactEvent);

	PrintStats();

	Log("main: сервер завершён");

	FreeLibrary(library);
	WSACleanup();

	DeleteCriticalSection(&commandLock);
	DeleteCriticalSection(&consoleLock);
	DeleteCriticalSection(&eventsLock);
	DeleteCriticalSection(&contactLock);
	return 0;
}
