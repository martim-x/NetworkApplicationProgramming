#pragma once

#include <string>
#include <windows.h>

const int CONSOLE_ANSWER_SIZE = 128;

std::string NormalizePipeName(const char* pipeName);

HANDLE ConnectConsole(const char* pipeName);

bool SendConsoleCommand(const char* pipeName, const std::string& command, std::string& answer);

bool IsKnownCommand(const std::string& command);

std::string TrimCommand(const std::string& command);

void PrintCommandHelp();
