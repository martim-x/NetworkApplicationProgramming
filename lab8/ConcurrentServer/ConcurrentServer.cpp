#define _WINSOCK_DEPRECATED_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#include "../ServiceLibrary/ServiceLibrary.h"
#include <iostream>
#include <string>
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
static int serviceTimeoutMs = 60000;
static int lastPriorityLevel = -1;
static FNSSS SSS = NULL;
static FNFindService FindService = NULL;

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

static void PrintStats()
{
	Log("--- Статистика сервера ---");
	Log("  подключений всего:  " + to_string(stats.totalConnections));
	Log("  активных сейчас:    " + to_string(stats.activeConnections));
	Log("  обслужено успешно:  " + to_string(stats.completedConnections));
	Log("  отказов:            " + to_string(stats.refusedConnections));
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

static void ProcessCommand()
{
	string cmd;
	if (!TakeCommand(cmd))
		return;

	if (cmd == "start")
	{
		InterlockedExchange(&accepting, 1);
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
		Log("AcceptServer: команда wait — ожидание завершения обслуживания клиентов");

		while (stats.activeConnections > 0 && !serverExit)
			Sleep(50);

		Log("AcceptServer: все подключённые клиенты обслужены");
	}
	else if (cmd == "shutdown")
	{
		InterlockedExchange(&accepting, 0);
		Log("AcceptServer: команда shutdown — приём запрещён, ждём обслуживания");

		while (stats.activeConnections > 0 && !serverExit)
			Sleep(50);

		InterlockedExchange(&serverExit, 1);
		Log("AcceptServer: сервер завершает работу (shutdown)");
	}
	else if (cmd == "exit")
	{
		InterlockedExchange(&serverExit, 1);
		Log("AcceptServer: сервер завершает работу (exit)");
	}
	else
	{
		Log("AcceptServer: неизвестная команда \"" + cmd + "\"");
	}
}

static VOID CALLBACK StartServiceAPC(ULONG_PTR param)
{
	int index = (int)param;

	EnterCriticalSection(&contactLock);
	ListContact& contact = contacts[index];

	if (contact.socket == INVALID_SOCKET)
	{
		LeaveCriticalSection(&contactLock);
		return;
	}

	Log("APC AcceptServer: старт обслуживания клиента " + EndpointToString(contact.address));
	LeaveCriticalSection(&contactLock);
}

static VOID CALLBACK FinishServiceAPC(ULONG_PTR param)
{
	int index = (int)param;

	EnterCriticalSection(&contactLock);
	ListContact& contact = contacts[index];
	double seconds = (GetTickCount() - contact.startedAt) / 1000.0;
	string endpoint = EndpointToString(contact.address);
	int messages = contact.messageCount;
	LeaveCriticalSection(&contactLock);

	char buffer[128];
	sprintf_s(buffer, sizeof(buffer), "%.3f", seconds);
	Log("APC AcceptServer: завершение обслуживания клиента " + endpoint +
		", сообщений " + to_string(messages) + ", время " + buffer + " с");
}

static ListContact* AddContact(SOCKET client, const SOCKADDR_IN& address)
{
	ListContact* result = NULL;

	EnterCriticalSection(&contactLock);

	for (int i = 0; i < MAX_CONTACTS; i++)
	{
		if (contacts[i].socket == INVALID_SOCKET)
		{
			contacts[i].socket = client;
			contacts[i].address = address;
			contacts[i].serviceThread = NULL;
			contacts[i].acceptThread = acceptThreadHandle;
			contacts[i].watchdog = NULL;
			contacts[i].busy = 0;
			contacts[i].finished = 0;
			contacts[i].startedAt = GetTickCount();
			contacts[i].messageCount = 0;
			contacts[i].pendingBytes = 0;
			memset(contacts[i].pending, 0, sizeof(contacts[i].pending));
			memset(contacts[i].request, 0, sizeof(contacts[i].request));
			result = &contacts[i];
			break;
		}
	}

	LeaveCriticalSection(&contactLock);
	return result;
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

	while (!serverExit)
	{
		SleepEx(0, TRUE);
		ProcessCommand();

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
		SetEvent(contactEvent);
	}

	closesocket(listenSocket);
	Log("AcceptServer: завершён");
	return 0;
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

			char buffer[MESSAGE_SIZE];
			memset(buffer, 0, sizeof(buffer));

			int bytes = recv(contact.socket, buffer, sizeof(buffer) - 1, 0);

			if (bytes == SOCKET_ERROR)
			{
				int err = WSAGetLastError();

				if (err == WSAEWOULDBLOCK)
					continue;

				Log("DispatchServer: recv: " + GetErrorMsgText(err));
				InterlockedExchange(&contact.finished, 1);
				continue;
			}

			if (bytes == 0)
			{
				InterlockedExchange(&contact.finished, 1);
				continue;
			}

			buffer[bytes] = '\0';

			FNService service = FindService(buffer);

			if (service == NULL)
			{
				send(contact.socket, MSG_ERROR_INQUIRY, (int)strlen(MSG_ERROR_INQUIRY) + 1, 0);
				InterlockedIncrement(&stats.refusedConnections);
				InterlockedExchange(&contact.finished, 1);
				Log("DispatchServer: неизвестный запрос \"" + string(buffer) + "\" от " +
					EndpointToString(contact.address) + ", отправлен ErrorInquiry");
				continue;
			}

			u_long blocking = 0;
			ioctlsocket(contact.socket, FIONBIO, &blocking);

			InterlockedExchange(&contact.busy, 1);
			strcpy_s(contact.request, buffer);
			contact.messageCount = 1;

			contact.pendingBytes = 0;
			int codeLength = (int)strlen(buffer);

			if (codeLength + 1 < bytes)
			{
				contact.pendingBytes = bytes - codeLength - 1;
				memcpy(contact.pending, buffer + codeLength + 1, contact.pendingBytes);
			}

			Log("DispatchServer: запрос \"" + string(buffer) + "\" от " +
				EndpointToString(contact.address) + " — запуск обслуживающего потока");

			ServiceRequest* request = new ServiceRequest();
			request->service = service;
			request->contact = &contact;

			if (SSS(request) != 0)
			{
				InterlockedIncrement(&stats.refusedConnections);
				InterlockedExchange(&contact.finished, 1);
				continue;
			}

			if (acceptThreadHandle != NULL)
				QueueUserAPC(StartServiceAPC, acceptThreadHandle, (ULONG_PTR)i);

			if (contact.watchdog != NULL)
			{
				CancelWaitableTimer(contact.watchdog);
				CloseHandle(contact.watchdog);
				contact.watchdog = NULL;
			}

			HANDLE watchdog = CreateWaitableTimerA(NULL, FALSE, NULL);

			if (watchdog != NULL)
			{
				LARGE_INTEGER dueTime;
				dueTime.QuadPart = -((LONGLONG)serviceTimeoutMs * 10000);

				if (SetWaitableTimer(watchdog, &dueTime, 0, NULL, NULL, FALSE))
					contact.watchdog = watchdog;
				else
					CloseHandle(watchdog);
			}
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

static DWORD WINAPI Watchdog(LPVOID)
{
	Log("Watchdog: запущен, лимит обслуживания клиента " + to_string(serviceTimeoutMs) + " мс");

	while (!serverExit)
	{
		HANDLE timers[MAX_CONTACTS];
		int indexes[MAX_CONTACTS];
		int count = 0;

		EnterCriticalSection(&contactLock);

		for (int i = 0; i < MAX_CONTACTS; i++)
		{
			ListContact& contact = contacts[i];

			if (contact.socket != INVALID_SOCKET && contact.watchdog != NULL && !contact.finished)
			{
				timers[count] = contact.watchdog;
				indexes[count] = i;
				count++;
			}
		}

		LeaveCriticalSection(&contactLock);

		if (count == 0)
		{
			Sleep(50);
			continue;
		}

		DWORD waitResult = WaitForMultipleObjects(count, timers, FALSE, 200);

		if (waitResult >= WAIT_OBJECT_0 && waitResult < WAIT_OBJECT_0 + count)
		{
			int index = indexes[waitResult - WAIT_OBJECT_0];

			EnterCriticalSection(&contactLock);
			ListContact& contact = contacts[index];

			if (contact.socket != INVALID_SOCKET && !contact.finished && contact.serviceThread != NULL)
			{
				Log("Watchdog: клиент " + EndpointToString(contact.address) +
					" превысил лимит " + to_string(serviceTimeoutMs) + " мс — поток завершается принудительно");
				TerminateThread(contact.serviceThread, 1);
				InterlockedIncrement(&stats.refusedConnections);
				InterlockedExchange(&contact.finished, 1);
			}

			if (contact.watchdog != NULL)
			{
				CloseHandle(contact.watchdog);
				contact.watchdog = NULL;
			}

			LeaveCriticalSection(&contactLock);
		}
	}

	Log("Watchdog: завершён");
	return 0;
}

static DWORD WINAPI GarbageCleaner(LPVOID)
{
	Log("GarbageCleaner: запущен");

	while (!serverExit)
	{
		Sleep(100);
		UpdatePriorities();

		EnterCriticalSection(&contactLock);

		for (int i = 0; i < MAX_CONTACTS; i++)
		{
			ListContact& contact = contacts[i];

			if (contact.socket == INVALID_SOCKET || !contact.finished)
				continue;

			if (acceptThreadHandle != NULL)
				QueueUserAPC(FinishServiceAPC, acceptThreadHandle, (ULONG_PTR)i);

			if (contact.serviceThread != NULL)
			{
				WaitForSingleObject(contact.serviceThread, 200);
				CloseHandle(contact.serviceThread);
				contact.serviceThread = NULL;
			}

			if (contact.watchdog != NULL)
			{
				CancelWaitableTimer(contact.watchdog);
				CloseHandle(contact.watchdog);
				contact.watchdog = NULL;
			}

			closesocket(contact.socket);
			contact.socket = INVALID_SOCKET;
			contact.busy = 0;
			contact.finished = 0;

			InterlockedDecrement(&stats.activeConnections);
			InterlockedIncrement(&stats.completedConnections);
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

	if ((hPipe = CreateNamedPipeA(config.pipeName,
		PIPE_ACCESS_DUPLEX,
		PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
		1,
		512,
		512,
		0,
		NULL)) == INVALID_HANDLE_VALUE)
	{
		Log("ConsolePipe: CreateNamedPipe: " + GetErrorMsgText(GetLastError()));
		return 1;
	}

	Log("ConsolePipe: канал управления создан: " + string(config.pipeName));

	while (!serverExit)
	{
		if (!ConnectNamedPipe(hPipe, NULL))
		{
			int err = GetLastError();
			if (err != ERROR_PIPE_CONNECTED)
			{
				Log("ConsolePipe: ConnectNamedPipe: " + GetErrorMsgText(err));
				Sleep(100);
				continue;
			}
		}

		memset(buffer, 0, sizeof(buffer));

		if (!ReadFile(hPipe, buffer, sizeof(buffer) - 1, &bytesRead, NULL))
		{
			int err = GetLastError();
			if (err != ERROR_BROKEN_PIPE && err != ERROR_NO_DATA)
				Log("ConsolePipe: ReadFile: " + GetErrorMsgText(err));

			DisconnectNamedPipe(hPipe);
			continue;
		}

		if (bytesRead == 0)
		{
			DisconnectNamedPipe(hPipe);
			continue;
		}

		buffer[bytesRead] = '\0';

		string cmd = buffer;
		bool known = (cmd == "start" || cmd == "stop" || cmd == "exit" ||
			cmd == "statistics" || cmd == "wait" || cmd == "shutdown");

		if (!known)
		{
			WriteFile(hPipe, MSG_NO_COMMAND, (DWORD)strlen(MSG_NO_COMMAND) + 1, &bytesWritten, NULL);
			FlushFileBuffers(hPipe);
			Log("ConsolePipe: неизвестная команда \"" + cmd + "\", отправлено nocmd");
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

int main(int argc, char* argv[])
{
	SetConsoleOutputCP(CP_UTF8);

	config.port = (argc > 1) ? atoi(argv[1]) : 2000;
	strcpy_s(config.dllName, (argc > 2) ? argv[2] : "ServiceLibrary.dll");
	strcpy_s(config.pipeName, (argc > 3) ? argv[3] : "\\\\.\\pipe\\ConcurrentServer");
	config.responsePort = (argc > 4) ? atoi(argv[4]) : 9200;
	strcpy_s(config.callSign, (argc > 5) ? argv[5] : "ConcurrentServer");
	serviceTimeoutMs = (argc > 6) ? atoi(argv[6]) : 60000;

	WSADATA wsaData;
	HMODULE library = NULL;
	HANDLE threads[4];
	int threadsCount = 0;

	InitializeCriticalSection(&contactLock);
	InitializeCriticalSection(&consoleLock);
	InitializeCriticalSection(&commandLock);

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

	SSS = (FNSSS)GetProcAddress(library, "SSS");
	FindService = (FNFindService)GetProcAddress(library, "FindService");

	if (SSS == NULL || FindService == NULL)
	{
		Log("main: в библиотеке не найдены экспортируемые функции SSS/FindService");
		FreeLibrary(library);
		return 1;
	}

	Log("main: библиотека " + string(config.dllName) + " загружена, SSS и FindService импортированы");

	acceptThreadHandle = CreateThread(NULL, 0, AcceptServer, NULL, 0, NULL);
	if (acceptThreadHandle == NULL)
	{
		Log("main: не удалось создать поток AcceptServer");
		return 1;
	}
	SetThreadPriority(acceptThreadHandle, THREAD_PRIORITY_ABOVE_NORMAL);
	threads[threadsCount++] = acceptThreadHandle;

	threads[threadsCount++] = CreateThread(NULL, 0, DispatchServer, NULL, 0, NULL);
	threads[threadsCount++] = CreateThread(NULL, 0, GarbageCleaner, NULL, 0, NULL);
	threads[threadsCount++] = CreateThread(NULL, 0, ConsolePipe, NULL, 0, NULL);

	HANDLE responseThread = CreateThread(NULL, 0, ResponseServer, NULL, 0, NULL);
	HANDLE watchdogThread = CreateThread(NULL, 0, Watchdog, NULL, 0, NULL);

	while (!serverExit)
		Sleep(50);

	Log("main: ожидание завершения потоков сервера");

	WaitForSingleObject(acceptThreadHandle, 5000);
	WaitForSingleObject(threads[1], 5000);
	WaitForSingleObject(threads[2], 5000);
	WaitForSingleObject(threads[3], 5000);
	WaitForSingleObject(responseThread, 5000);
	WaitForSingleObject(watchdogThread, 5000);

	for (int i = 0; i < threadsCount; i++)
		CloseHandle(threads[i]);

	CloseHandle(responseThread);
	CloseHandle(watchdogThread);

	for (int i = 0; i < MAX_CONTACTS; i++)
	{
		if (contacts[i].socket != INVALID_SOCKET)
			closesocket(contacts[i].socket);
		if (contacts[i].serviceThread != NULL)
			CloseHandle(contacts[i].serviceThread);
	}

	CloseHandle(contactEvent);

	PrintStats();

	FreeLibrary(library);
	WSACleanup();

	DeleteCriticalSection(&commandLock);
	DeleteCriticalSection(&consoleLock);
	DeleteCriticalSection(&contactLock);

	Log("main: сервер завершён");
	return 0;
}
