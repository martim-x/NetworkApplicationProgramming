#define _WINSOCK_DEPRECATED_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#include "../ConsoleApi/ConsoleApi.h"
#include <iostream>
#include <string>
#include <sstream>
#include <cstring>
using namespace std;

// Печать точной причины неудачи: операция канала, код ошибки и её текст.
static void ReportFailure(const string& command, const ConsoleResult& result)
{
	cout << "RConsole: команда \"" << command << "\" не выполнена: операция "
		<< result.operation << ", код " << result.lastError;

	if (!result.errorText.empty())
		cout << " (" << result.errorText << ")";

	cout << endl;
}

static bool RunCommand(const string& pipeName, const string& command)
{
	ConsoleResult result;

	if (!ExecuteConsoleCommand(pipeName.c_str(), command, CONSOLE_DEFAULT_TIMEOUT_MS, result))
	{
		ReportFailure(command, result);

		if (result.status == CONSOLE_CONNECT_FAILED)
			cout << "RConsole: сервер должен быть запущен до консоли" << endl;

		return false;
	}

	cout << "RConsole: " << command << " -> " << result.payload << endl;
	return true;
}

// Опрос диагностических событий сервера (служебная команда events).
// События подключения/отключения/отказа/таймаута забираются из очереди
// сервера и печатаются в консоли; пустой ответ ("-") ничего не печатает.
static void PollEvents(const string& pipeName)
{
	ConsoleResult result;

	if (!ExecuteConsoleCommand(pipeName.c_str(), "events", CONSOLE_DEFAULT_TIMEOUT_MS, result))
		return;

	if (result.payload.empty() || result.payload == "-")
		return;

	istringstream lines(result.payload);
	string line;

	while (getline(lines, line))
	{
		if (!line.empty())
			cout << "RConsole: событие: " << line << endl;
	}
}

int main(int argc, char* argv[])
{
	SetConsoleOutputCP(CP_UTF8);

	string pipeName = NormalizePipeName((argc > 1) ? argv[1] : NULL);

	cout << "RConsole: удалённая консоль сервера ConcurrentServer" << endl;
	cout << "RConsole: канал управления " << pipeName << endl;

	bool interactive = (argc <= 2);
	if (interactive)
		PrintCommandHelp();

	// Пробного подключения нет: первая же команда сама устанавливает соединение.

	if (argc > 2)
	{
		for (int i = 2; i < argc; i++)
		{
			string command = TrimCommand(argv[i]);

			if (command == "help")
			{
				PrintCommandHelp();
				continue;
			}

			if (!IsKnownCommand(command))
			{
				cout << "RConsole: команда \"" << command << "\" неизвестна" << endl;
				continue;
			}

			if (!RunCommand(pipeName, command))
				return 1;

			if (command == "exit" || command == "shutdown")
				break;

			PollEvents(pipeName);
		}

		return 0;
	}

	while (true)
	{
		cout << "rconsole> ";

		string line;
		if (!getline(cin, line))
			break;

		string command = TrimCommand(line);

		if (command.empty())
			continue;

		if (command == "quit")
		{
			cout << "RConsole: выход без завершения сервера" << endl;
			break;
		}

		if (command == "help")
		{
			PrintCommandHelp();
			continue;
		}

		if (!IsKnownCommand(command))
		{
			cout << "RConsole: команда \"" << command << "\" неизвестна (help — список команд)" << endl;
			continue;
		}

		if (!RunCommand(pipeName, command))
			return 1;

		if (command == "exit" || command == "shutdown")
		{
			cout << "RConsole: сервер завершает работу" << endl;
			break;
		}

		PollEvents(pipeName);
	}

	return 0;
}
