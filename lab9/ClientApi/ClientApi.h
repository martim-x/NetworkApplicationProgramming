#pragma once

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <string>

struct ServerAddress
{
	SOCKADDR_IN address;
	std::string ip;
	short port;
};

std::string GetErrorMsgText(int code);

void PrintError(const std::string& operation, int code);

std::string GetLocalBroadcastAddress();

bool FindServer(const char* callSign, short udpPort, short tcpPort, ServerAddress& server, int timeoutMs);

SOCKET ConnectToServer(const ServerAddress& server);

bool SendText(SOCKET s, const char* text);

int ReceiveText(SOCKET s, char* buffer, int size);

bool SendRequest(SOCKET s, const char* code);

bool RequestEcho(SOCKET s, const char* text, char* answer, int size);

bool RequestTime(SOCKET s, char* answer, int size);

bool RequestRand(SOCKET s, int& value);

bool SendInvalidRequest(SOCKET s, char* answer, int size);

double ElapsedSeconds(DWORD startTick);
