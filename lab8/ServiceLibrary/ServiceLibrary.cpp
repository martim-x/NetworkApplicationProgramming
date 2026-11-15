#define _WINSOCK_DEPRECATED_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#include "ServiceLibrary.h"
#include <iostream>
#include <string>
#include <cstring>
#include <cstdio>
#include <ctime>
#include <cstdlib>
#pragma comment(lib, "WS2_32.lib")
using namespace std;

static CRITICAL_SECTION consoleLock;
static bool consoleLockReady = false;

static void Log(const string& text)
{
	if (consoleLockReady)
		EnterCriticalSection(&consoleLock);

	cout << text << endl;

	if (consoleLockReady)
		LeaveCriticalSection(&consoleLock);
}

static string TimeToString()
{
	time_t now = time(NULL);
	struct tm local;
	localtime_s(&local, &now);

	char buffer[32];
	strftime(buffer, sizeof(buffer), "%d.%m.%Y/%H:%M:%S", &local);
	return buffer;
}

static DWORD WINAPI EchoServer(LPVOID param)
{
	ListContact* contact = (ListContact*)param;
	char buffer[MESSAGE_SIZE];

	Log("  EchoServer: обслуживание клиента начато");

	if (contact->pendingBytes > 0)
	{
		contact->messageCount++;

		if (send(contact->socket, contact->pending, contact->pendingBytes, 0) == SOCKET_ERROR)
		{
			InterlockedExchange(&contact->failed, 1);
			InterlockedExchange(&contact->finished, 1);
			return 0;
		}
	}

	while (true)
	{
		int bytes = recv(contact->socket, buffer, sizeof(buffer) - 1, 0);

		if (bytes == SOCKET_ERROR)
		{
			InterlockedExchange(&contact->failed, 1);
			break;
		}

		if (bytes == 0)
			break;

		buffer[bytes] = '\0';
		contact->messageCount++;

		if (send(contact->socket, buffer, bytes, 0) == SOCKET_ERROR)
		{
			InterlockedExchange(&contact->failed, 1);
			break;
		}
	}

	Log("  EchoServer: обслуживание клиента завершено, сообщений: " +
		to_string(contact->messageCount));

	InterlockedExchange(&contact->finished, 1);
	return 0;
}

static DWORD WINAPI TimeServer(LPVOID param)
{
	ListContact* contact = (ListContact*)param;
	char buffer[MESSAGE_SIZE];

	Log("  TimeServer: обслуживание клиента начато");

	string answer = TimeToString();
	if (send(contact->socket, answer.c_str(), (int)answer.size() + 1, 0) == SOCKET_ERROR)
	{
		InterlockedExchange(&contact->failed, 1);
		InterlockedExchange(&contact->finished, 1);
		return 0;
	}

	while (true)
	{
		int bytes = recv(contact->socket, buffer, sizeof(buffer) - 1, 0);

		if (bytes == 0)
			break;

		if (bytes == SOCKET_ERROR)
		{
			InterlockedExchange(&contact->failed, 1);
			break;
		}

		buffer[bytes] = '\0';
		contact->messageCount++;

		if (strcmp(buffer, "time") != 0)
			break;

		answer = TimeToString();
		if (send(contact->socket, answer.c_str(), (int)answer.size() + 1, 0) == SOCKET_ERROR)
		{
			InterlockedExchange(&contact->failed, 1);
			break;
		}
	}

	Log("  TimeServer: обслуживание клиента завершено, сообщений: " +
		to_string(contact->messageCount));

	InterlockedExchange(&contact->finished, 1);
	return 0;
}

static DWORD WINAPI RandServer(LPVOID param)
{
	ListContact* contact = (ListContact*)param;
	char buffer[MESSAGE_SIZE];

	Log("  RandServer: обслуживание клиента начато");

	srand((unsigned)time(NULL) ^ GetCurrentThreadId());

	int value = rand();
	if (send(contact->socket, (const char*)&value, sizeof(value), 0) == SOCKET_ERROR)
	{
		InterlockedExchange(&contact->failed, 1);
		InterlockedExchange(&contact->finished, 1);
		return 0;
	}

	while (true)
	{
		int bytes = recv(contact->socket, buffer, sizeof(buffer) - 1, 0);

		if (bytes == 0)
			break;

		if (bytes == SOCKET_ERROR)
		{
			InterlockedExchange(&contact->failed, 1);
			break;
		}

		buffer[bytes] = '\0';
		contact->messageCount++;

		if (strcmp(buffer, "rand") != 0)
			break;

		value = rand();
		if (send(contact->socket, (const char*)&value, sizeof(value), 0) == SOCKET_ERROR)
		{
			InterlockedExchange(&contact->failed, 1);
			break;
		}
	}

	Log("  RandServer: обслуживание клиента завершено, сообщений: " +
		to_string(contact->messageCount));

	InterlockedExchange(&contact->finished, 1);
	return 0;
}

static TableService services[] =
{
	{ "echo", EchoServer },
	{ "time", TimeServer },
	{ "rand", RandServer }
};

static const int servicesCount = sizeof(services) / sizeof(services[0]);

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID)
{
	if (reason == DLL_PROCESS_ATTACH)
	{
		InitializeCriticalSection(&consoleLock);
		consoleLockReady = true;
	}
	else if (reason == DLL_PROCESS_DETACH)
	{
		if (consoleLockReady)
		{
			DeleteCriticalSection(&consoleLock);
			consoleLockReady = false;
		}
	}

	return TRUE;
}

extern "C" __declspec(dllexport) FNService WINAPI FindService(const char* code)
{
	for (int i = 0; i < servicesCount; i++)
	{
		if (strcmp(services[i].code, code) == 0)
			return services[i].service;
	}

	return NULL;
}

extern "C" __declspec(dllexport) int WINAPI GetServicesCount()
{
	return servicesCount;
}

extern "C" __declspec(dllexport) const char* WINAPI GetServiceCode(int index)
{
	if (index < 0 || index >= servicesCount)
		return NULL;

	return services[index].code;
}

extern "C" __declspec(dllexport) DWORD WINAPI SSS(LPVOID param)
{
	ServiceRequest* request = (ServiceRequest*)param;

	if (request == NULL || request->contact == NULL || request->service == NULL)
	{
		delete request;
		return 1;
	}

	ListContact* contact = request->contact;
	FNService service = request->service;
	delete request;

	contact->serviceThread = CreateThread(NULL, 0, service, contact, 0, NULL);

	if (contact->serviceThread == NULL)
	{
		Log("  SSS: не удалось создать обслуживающий поток");
		InterlockedExchange(&contact->failed, 1);
		InterlockedExchange(&contact->finished, 1);
		return 1;
	}

	SetThreadPriority(contact->serviceThread, THREAD_PRIORITY_BELOW_NORMAL);
	return 0;
}
