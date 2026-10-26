#define _WINSOCK_DEPRECATED_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#include "../ConsoleApi/ConsoleApi.h"
#include <iostream>
#include <string>
#include <cstring>
using namespace std;

int main(int argc, char* argv[])
{
	SetConsoleOutputCP(CP_UTF8);

	const char* pipeName = (argc > 1) ? argv[1] : "\\\\.\\pipe\\ConcurrentServer";

	cout << "RConsole: удалённая консоль сервера ConcurrentServer" << endl;
	cout << "RConsole: канал управления " << pipeName << endl;

	HANDLE pipe = ConnectConsole(pipeName);
	if (pipe == INVALID_HANDLE_VALUE)
	{
		cout << "RConsole: не удалось подключиться к каналу, код " << GetLastError() << endl;
		cout << "RConsole: сервер должен быть запущен до консоли" << endl;
		return 1;
	}

	cout << "RConsole: соединение с сервером установлено" << endl;
	CloseHandle(pipe);

	bool interactive = (argc <= 2);
	if (interactive)
		PrintCommandHelp();

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

			string answer;
			if (!SendConsoleCommand(pipeName, command, answer))
			{
				cout << "RConsole: сервер не ответил на команду \"" << command << "\"" << endl;
				break;
			}

			cout << "RConsole: " << command << " -> " << answer << endl;

			if (command == "exit" || command == "shutdown")
				break;
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

		string answer;
		if (!SendConsoleCommand(pipeName, command, answer))
		{
			cout << "RConsole: сервер не ответил на команду \"" << command << "\"" << endl;
			break;
		}

		cout << "RConsole: сервер подтвердил команду \"" << answer << "\"" << endl;

		if (command == "exit" || command == "shutdown")
		{
			cout << "RConsole: сервер завершает работу" << endl;
			break;
		}
	}

	return 0;
}
