#pragma once

#include <string>
#include <windows.h>

// Размер буфера ответа одной команды канала. Ответ events собирается из
// нескольких событий, поэтому буфер рассчитан на очередь целиком; более
// длинное сообщение дочитывается по ERROR_MORE_DATA.
const int CONSOLE_ANSWER_SIZE = 1024;

// Таймаут по умолчанию для всех операций канала: подключение, запись и чтение
// вместе ограничены одним дедлайном, чтобы консоль не ждала сервер бесконечно.
const DWORD CONSOLE_DEFAULT_TIMEOUT_MS = 5000;

// Транспортный статус операции канала. Смысл ответа (payload) не
// интерпретируется: это ответственность вызывающего кода.
enum ConsoleStatus
{
	CONSOLE_OK = 0,
	CONSOLE_CONNECT_FAILED,
	CONSOLE_WRITE_FAILED,
	CONSOLE_READ_FAILED,
	CONSOLE_TIMEOUT
};

// Результат одной операции: статус, полученные данные (транспортный уровень),
// точная последняя ошибка Windows и операция, на которой она возникла.
struct ConsoleResult
{
	ConsoleStatus status;
	std::string payload;
	std::string operation;
	DWORD lastError;
	std::string errorText;

	ConsoleResult()
		: status(CONSOLE_OK), payload(), operation(), lastError(0), errorText() {}
};

std::string NormalizePipeName(const char* pipeName);

// Подключение с дедлайном: ERROR_PIPE_BUSY обрабатывается ожиданием
// WaitNamedPipe, но суммарное ожидание ограничено timeoutMs.
// lastError получает точный код последней неудачной попытки.
HANDLE ConnectConsoleTimeout(const char* pipeName, DWORD timeoutMs, DWORD& lastError);

// Выполнение команды за конечное время: подключение, запись команды и чтение
// ответа ограничены общим дедлайном timeoutMs. По таймауту чтение отменяется
// (CancelIoEx), поэтому сервер-«молчун» не блокирует консоль навсегда.
// Возвращает true только при полученном ответе; result содержит статус,
// payload, операцию и точный код ошибки.
bool ExecuteConsoleCommand(const char* pipeName, const std::string& command,
	DWORD timeoutMs, ConsoleResult& result);

// Обратная совместимость: прежний интерфейс через ExecuteConsoleCommand
// со стандартным таймаутом CONSOLE_DEFAULT_TIMEOUT_MS.
bool SendConsoleCommand(const char* pipeName, const std::string& command, std::string& answer);

bool IsKnownCommand(const std::string& command);

std::string TrimCommand(const std::string& command);

void PrintCommandHelp();
