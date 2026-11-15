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

// Поиск сервера по имени хоста (getaddrinfo) вместо широковещания.
// Перебирает IPv4-адреса имени, подключается по TCP с ограничением по timeoutMs.
// false: имя не разрешилось, ни один адрес недоступен или вышло время.
bool FindServerByName(const char* hostname, short tcpPort, ServerAddress& server, int timeoutMs);

SOCKET ConnectToServer(const ServerAddress& server);

// Передача данных с гарантией полного проталкивания: цикл send до конца буфера
// (частичная отправка TCP нормальна и не считается успехом).
bool SendAllBytes(SOCKET s, const char* data, int bytes);

bool SendText(SOCKET s, const char* text);

// Приём следующего NUL-завершённого текстового фрейма с внутренним буфером на сокет:
// данные, пришедшие частями или несколько фреймов одним пакетом, разбираются корректно.
// Возвращает длину фрейма (>= 0) или SOCKET_ERROR: EOF среди фрейма, таймаут приёма,
// обрыв соединения, фрейм без NUL свыше 1 МБ или не помещающийся в buffer размера size.
int ReceiveText(SOCKET s, char* buffer, int size);

// Строгий разбор целого числа: только необязательный знак и цифры,
// без переполнения int. "12abc", "", "99999999999" — false.
bool ParseIntStrict(const char* text, int& value);

bool SendRequest(SOCKET s, const char* code);

bool RequestEcho(SOCKET s, const char* text, char* answer, int size);

bool RequestTime(SOCKET s, char* answer, int size);

bool RequestRand(SOCKET s, int& value);

bool SendInvalidRequest(SOCKET s, char* answer, int size);

double ElapsedSeconds(DWORD startTick);
