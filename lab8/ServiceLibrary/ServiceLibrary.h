#pragma once

#include <winsock2.h>
#include <windows.h>

const int MAX_CONTACTS = 64;
const int COMMAND_SIZE = 32;
const int MESSAGE_SIZE = 1024;

struct ListContact
{
	SOCKET socket;
	SOCKADDR_IN address;
	HANDLE serviceThread;
	HANDLE acceptThread;
	HANDLE watchdog;
	volatile LONG busy;
	volatile LONG finished;
	DWORD startedAt;
	int messageCount;
	char request[32];
	char pending[MESSAGE_SIZE];
	int pendingBytes;
};

struct StatsInfo
{
	volatile LONG totalConnections;
	volatile LONG activeConnections;
	volatile LONG refusedConnections;
	volatile LONG completedConnections;
};

typedef DWORD(WINAPI* FNService)(LPVOID param);
typedef DWORD(WINAPI* FNSSS)(LPVOID param);
typedef FNService(WINAPI* FNFindService)(const char* code);

struct TableService
{
	const char* code;
	FNService service;
};

struct ServiceRequest
{
	FNService service;
	ListContact* contact;
};

const char* const CMD_GET_COMMAND = "getcommand";
const char* const MSG_ERROR_INQUIRY = "ErrorInquiry";
const char* const MSG_NO_COMMAND = "nocmd";
