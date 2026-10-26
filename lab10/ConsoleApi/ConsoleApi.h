#pragma once

#include <windows.h>
#include <string>

const int CONSOLE_ANSWER_SIZE = 128;

HANDLE ConnectConsole(const char* pipeName);

bool SendConsoleCommand(const char* pipeName, const std::string& command, std::string& answer);

bool IsKnownCommand(const std::string& command);

std::string TrimCommand(const std::string& command);

void PrintCommandHelp();
