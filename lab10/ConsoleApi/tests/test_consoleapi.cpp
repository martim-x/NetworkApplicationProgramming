// Тесты ConsoleApi (ПР10): конечное время операций канала, точная последняя ошибка.
// Фикстура — собственный тестовый сервер канала, поэтому тесты не зависят от сервера ЛР8
// и не предполагают новый формат ответов (проверяется только транспорт: статус + payload).
//
// Сборка (файл включает ConsoleApi.cpp напрямую, поэтому отдельный cl без аргументов):
//   cl /nologo /EHsc /utf-8 /W3 ConsoleApi\tests\test_consoleapi.cpp /Fe:lab10\build\test_consoleapi.exe
//
// Запуск из каталога lab10: build\test_consoleapi.exe

#define _CRT_SECURE_NO_WARNINGS
#include "../ConsoleApi.cpp"

#include <windows.h>
#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <cstdio>

using namespace std;

static atomic<int> g_result(0); // 0 = не завершён, 1 = pass, 2 = fail
static string g_failReason;
static mutex g_failMutex;

static void TestFail(const string& reason)
{
    lock_guard<mutex> lock(g_failMutex);
    if (g_result == 0)
    {
        g_result = 2;
        g_failReason = reason;
    }
}

static void TestPass(void)
{
    g_result = 1;
}

static void ExpectTrue(bool condition, const string& what)
{
    if (!condition)
        TestFail(what);
}

// ---------------- фикстура: тестовый сервер канала ----------------

enum FixtureBehavior { FIX_ECHO, FIX_NOCMD, FIX_SILENT, FIX_DISCONNECT };

static void FixturePipeWorker(const string& fullName, FixtureBehavior behavior)
{
    HANDLE pipe = CreateNamedPipeA(fullName.c_str(),
        PIPE_ACCESS_DUPLEX,
        PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
        1, 512, 512, 0, NULL);

    if (pipe == INVALID_HANDLE_VALUE)
        return;

    BOOL connected = ConnectNamedPipe(pipe, NULL);
    if (!connected && GetLastError() != ERROR_PIPE_CONNECTED)
    {
        CloseHandle(pipe);
        return;
    }

    char buffer[512];
    DWORD bytesRead = 0;

    if (ReadFile(pipe, buffer, sizeof(buffer) - 1, &bytesRead, NULL) && bytesRead > 0)
    {
        buffer[bytesRead] = '\0';

        DWORD bytesWritten = 0;

        if (behavior == FIX_ECHO)
            WriteFile(pipe, buffer, bytesRead, &bytesWritten, NULL);
        else if (behavior == FIX_NOCMD)
            WriteFile(pipe, "nocmd", 6, &bytesWritten, NULL);
        else if (behavior == FIX_DISCONNECT)
            FlushFileBuffers(pipe); // ответ не пишем: клиент увидит разрыв канала
        else if (behavior == FIX_SILENT)
        {
            // Молчание = канал остаётся открытым и без ответа до истечения
            // дедлайна клиента. Немедленный DisconnectNamedPipe дал бы клиенту
            // ERROR_BROKEN_PIPE вместо таймаута чтения.
            Sleep(4000);
        }
    }

    DisconnectNamedPipe(pipe);
    CloseHandle(pipe);
}

static void StartFixture(const string& fullName, FixtureBehavior behavior)
{
    thread(FixturePipeWorker, fullName, behavior).detach();
}

static string UniquePipeName(const char* tag)
{
    static int counter = 0;
    char buffer[128];
    sprintf_s(buffer, "\\\\.\\pipe\\ConsoleApiTest_%lu_%d",
        (unsigned long)GetCurrentProcessId(), ++counter);
    (void)tag;
    return string(buffer);
}

// ---------------- запуск теста под сторожевым таймером ----------------

static void RunWatched(const char* name, void (*test)(), int watchdogMs)
{
    g_result = 0;
    g_failReason.clear();

    thread worker(test);
    HANDLE handle = worker.native_handle();

    DWORD waited = WaitForSingleObject(handle, (DWORD)watchdogMs);

    if (waited == WAIT_TIMEOUT)
    {
        printf("FAIL %s (тест не завершился за %d мс — операция зависла)\n", name, watchdogMs);
        TerminateThread(handle, 1); // процесс всё равно завершится с кодом 1
        fflush(stdout);
        return;
    }

    worker.join();

    if (g_result == 1)
        printf("PASS %s\n", name);
    else
        printf("FAIL %s: %s\n", name, g_failReason.c_str());

    fflush(stdout);
}

// ---------------- тесты ----------------

// 1. Канала нет: CONNECT_FAILED, точный код ошибки (2), конечное время.
static void test_missing_pipe_reports_connect_error(void)
{
    string pipeName = UniquePipeName("missing");

    ConsoleResult result;
    bool sent = ExecuteConsoleCommand(pipeName.c_str(), "statistics", 1000, result);

    ExpectTrue(!sent, "команда не должна быть отправлена, когда канала нет");
    ExpectTrue(result.status == CONSOLE_CONNECT_FAILED, "status == CONSOLE_CONNECT_FAILED");
    ExpectTrue(result.lastError == ERROR_FILE_NOT_FOUND, "lastError == 2 (ERROR_FILE_NOT_FOUND)");
    ExpectTrue(result.operation == "CreateFile", "operation == CreateFile");
    ExpectTrue(!result.errorText.empty(), "errorText не пуст");

    TestPass();
}

// 2. Сервер подключён, но не отвечает: таймаут чтения за конечное время,
//    и следующая команда к живому серверу проходит.
static void test_silent_server_times_out_finite(void)
{
    string pipeName = UniquePipeName("silent");
    StartFixture(pipeName, FIX_SILENT);
    Sleep(150); // дать фикстуре создать экземпляр канала

    ConsoleResult result;
    auto begin = chrono::steady_clock::now();
    bool sent = ExecuteConsoleCommand(pipeName.c_str(), "statistics", 500, result);
    auto elapsed = chrono::steady_clock::now() - begin;

    ExpectTrue(!sent, "команда без ответа сервера не может быть подтверждена");
    ExpectTrue(result.status == CONSOLE_TIMEOUT, "status == CONSOLE_TIMEOUT");
    ExpectTrue(result.operation == "ReadFile", "operation == ReadFile");
    ExpectTrue(elapsed < chrono::seconds(3), "операция завершилась за конечное время");

    // после молчащего клиента сервер снова доступен
    string liveName = UniquePipeName("live");
    StartFixture(liveName, FIX_ECHO);
    Sleep(150);

    ConsoleResult after;
    bool sentAfter = ExecuteConsoleCommand(liveName.c_str(), "start", 2000, after);

    ExpectTrue(sentAfter, "после таймаута следующая команда проходит");
    ExpectTrue(after.status == CONSOLE_OK, "status == CONSOLE_OK");
    ExpectTrue(after.payload == "start", "payload — ответ сервера (эхо фикстуры)");

    TestPass();
}

// 3. Успешный обмен: статус OK, payload — то, что вернул сервер.
static void test_send_returns_server_payload(void)
{
    string pipeName = UniquePipeName("echo");
    StartFixture(pipeName, FIX_ECHO);
    Sleep(150);

    ConsoleResult result;
    bool sent = ExecuteConsoleCommand(pipeName.c_str(), "statistics", 2000, result);

    ExpectTrue(sent, "команда отправлена и ответ получен");
    ExpectTrue(result.status == CONSOLE_OK, "status == CONSOLE_OK");
    ExpectTrue(result.payload == "statistics", "payload == ответ сервера");
    ExpectTrue(result.lastError == 0, "lastError == 0 при успехе");

    TestPass();
}

// 4. Транспорт не интерпретирует ответ: nocmd доставляется как payload со статусом OK.
static void test_nocmd_payload_delivered_as_is(void)
{
    string pipeName = UniquePipeName("nocmd");
    StartFixture(pipeName, FIX_NOCMD);
    Sleep(150);

    ConsoleResult result;
    bool sent = ExecuteConsoleCommand(pipeName.c_str(), "reload", 2000, result);

    ExpectTrue(sent, "ответ сервера получен");
    ExpectTrue(result.status == CONSOLE_OK, "статус транспортный (OK), смысл ответа решает вызывающий");
    ExpectTrue(result.payload == "nocmd", "payload == nocmd");

    TestPass();
}

// 5. Сервер закрывает канал без ответа: READ_FAILED, точная операция.
static void test_disconnected_pipe_reports_read_failed(void)
{
    string pipeName = UniquePipeName("disconnect");
    StartFixture(pipeName, FIX_DISCONNECT);
    Sleep(150);

    ConsoleResult result;
    bool sent = ExecuteConsoleCommand(pipeName.c_str(), "statistics", 2000, result);

    ExpectTrue(!sent, "ответа не было — успех невозможен");
    ExpectTrue(result.status == CONSOLE_READ_FAILED, "status == CONSOLE_READ_FAILED");
    ExpectTrue(result.operation == "ReadFile", "operation == ReadFile");
    ExpectTrue(result.lastError == ERROR_BROKEN_PIPE, "lastError == 109 (ERROR_BROKEN_PIPE)");

    TestPass();
}

// 6. Все экземпляры канала заняты (клиент подключился, но команду не отправил):
//    дедлайн подключения ограничивает ожидание; после освобождения канала
//    следующая команда проходит. Незавершённый клиент не блокирует управление навсегда.
static void test_busy_instance_bounded_by_deadline(void)
{
    string pipeName = UniquePipeName("busy");

    // держим единственный экземпляр подключённым «немым» клиентом
    HANDLE holderServer = CreateNamedPipeA(pipeName.c_str(),
        PIPE_ACCESS_DUPLEX,
        PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
        1, 512, 512, 0, NULL);
    ExpectTrue(holderServer != INVALID_HANDLE_VALUE, "фикстура создала экземпляр канала");
    if (holderServer == INVALID_HANDLE_VALUE)
        return;

    HANDLE holderClient = CreateFileA(pipeName.c_str(),
        GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    ExpectTrue(holderClient != INVALID_HANDLE_VALUE, "немой клиент занял экземпляр канала");
    if (holderClient == INVALID_HANDLE_VALUE)
    {
        CloseHandle(holderServer);
        return;
    }

    ConsoleResult result;
    auto begin = chrono::steady_clock::now();
    bool sent = ExecuteConsoleCommand(pipeName.c_str(), "statistics", 400, result);
    auto elapsed = chrono::steady_clock::now() - begin;

    ExpectTrue(!sent, "занятый экземпляр не может дать ответ");
    ExpectTrue(result.status == CONSOLE_CONNECT_FAILED, "status == CONSOLE_CONNECT_FAILED");
    ExpectTrue(result.lastError == ERROR_PIPE_BUSY, "lastError == 231 (ERROR_PIPE_BUSY)");
    ExpectTrue(elapsed < chrono::seconds(3), "ожидание ограничено дедлайном");

    CloseHandle(holderClient);
    DisconnectNamedPipe(holderServer);
    CloseHandle(holderServer);

    // после освобождения — обычный обмен
    string liveName = UniquePipeName("afterbusy");
    StartFixture(liveName, FIX_ECHO);
    Sleep(150);

    ConsoleResult after;
    bool sentAfter = ExecuteConsoleCommand(liveName.c_str(), "stop", 2000, after);

    ExpectTrue(sentAfter, "после освобождения канала команда проходит");
    ExpectTrue(after.status == CONSOLE_OK, "status == CONSOLE_OK");

    TestPass();
}

// 7. Обратная совместимость: SendConsoleCommand продолжает работать и
//    больше не зависает на молчащем сервере.
static void test_legacy_send_console_command_still_works(void)
{
    string pipeName = UniquePipeName("legacy");
    StartFixture(pipeName, FIX_ECHO);
    Sleep(150);

    string answer;
    bool sent = SendConsoleCommand(pipeName.c_str(), "start", answer);

    ExpectTrue(sent, "legacy SendConsoleCommand работает");
    ExpectTrue(answer == "start", "legacy answer == ответ сервера");

    // молчащий сервер: зависание недопустимо (возврат за конечное время)
    string silentName = UniquePipeName("legacy-silent");
    StartFixture(silentName, FIX_SILENT);
    Sleep(150);

    auto begin = chrono::steady_clock::now();
    string silentAnswer;
    bool silentSent = SendConsoleCommand(silentName.c_str(), "statistics", silentAnswer);
    auto elapsed = chrono::steady_clock::now() - begin;

    ExpectTrue(!silentSent, "молчащий сервер не может подтвердить команду");
    ExpectTrue(elapsed < chrono::seconds(10), "legacy-вызов завершился за конечное время");

    TestPass();
}

int main(void)
{
    // имена каналов уникальны для процесса, но прогон идёт последовательно
    RunWatched("missing_pipe_reports_connect_error", test_missing_pipe_reports_connect_error, 10000);
    RunWatched("silent_server_times_out_finite", test_silent_server_times_out_finite, 20000);
    RunWatched("send_returns_server_payload", test_send_returns_server_payload, 10000);
    RunWatched("nocmd_payload_delivered_as_is", test_nocmd_payload_delivered_as_is, 10000);
    RunWatched("disconnected_pipe_reports_read_failed", test_disconnected_pipe_reports_read_failed, 10000);
    RunWatched("busy_instance_bounded_by_deadline", test_busy_instance_bounded_by_deadline, 20000);
    RunWatched("legacy_send_console_command_still_works", test_legacy_send_console_command_still_works, 30000);

    printf("Готово: тесты ConsoleApi\n");
    return 0;
}
