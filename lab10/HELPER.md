# ЛР10 — команды сборки и запуска (удалённая консоль)

## 1. Сборка

```cmd
cd /d "%USERPROFILE%\Documents\Visual Studio Code\NetworkApplicationProgramming"
if not exist lab10\build mkdir lab10\build
cl /nologo /EHsc /utf-8 /W3 lab10\RConsole\RConsole.cpp lab10\ConsoleApi\ConsoleApi.cpp /Fe:lab10\build\RConsole.exe
```

| Ключ | Что делает |
|---|---|
| `/EHsc` | стандартная обработка исключений C++ |
| `/utf-8` | исходники в UTF-8 (иначе русский текст ломается) |
| `/W3` | уровень предупреждений |
| `/Fe:` | имя выходного exe |

## 2. Запуск сервера (ЛР8)

```cmd
cd /d "%USERPROFILE%\Documents\Visual Studio Code\NetworkApplicationProgramming\lab8\build"
ConcurrentServer.exe 9301 ServiceLibrary.dll \\.\pipe\TestConsole 9300 TestServer
```

Канал управления — третий аргумент (`\\.\pipe\TestConsole` в примере).

## 3. Сценарий из команд

```cmd
cd /d "%USERPROFILE%\Documents\Visual Studio Code\NetworkApplicationProgramming\lab10\build"
RConsole.exe \\.\pipe\TestConsole statistics stop start statistics
```

Ожидаемый вывод:

```
RConsole: соединение с сервером установлено
RConsole: statistics -> statistics
RConsole: stop -> stop
RConsole: start -> start
RConsole: statistics -> statistics
```

## 4. Интерактивный режим

```cmd
RConsole.exe \\.\pipe\TestConsole
```

```
rconsole> help
rconsole> statistics
rconsole> quit
```

`quit` — выйти, не завершая сервер. `exit` и `shutdown` завершают сервер.

## 5. Проверка отсутствия сервера

```cmd
RConsole.exe \\.\pipe\NoSuchConsole statistics
```

Ожидаемо: `RConsole: не удалось подключиться к каналу, код 2`.

## 6. Завершение сервера из консоли

```cmd
RConsole.exe \\.\pipe\TestConsole shutdown
```

Сервер дожидается обслуживания активных клиентов и завершается; в его журнале — `AcceptServer: сервер завершает работу (shutdown)` и итоговая статистика.

## 7. Разбор ошибок

| Сообщение | Причина | Лечение |
|---|---|---|
| `не удалось подключиться к каналу, код 2` | сервер не запущен или другое имя канала | проверить третий аргумент `ConcurrentServer` |
| `не удалось подключиться, код 231` (`ERROR_PIPE_BUSY`) | все экземпляры канала заняты | повторить (API уже ждёт `WaitNamedPipe`) |
| `сервер не ответил на команду` | сервер закрыл канал до ответа (например, при `shutdown` без клиентов) | проверить журнал сервера |
| `команда "..." неизвестна` | опечатка или команда не из таблицы 7.9.3 | `help` — список команд |
| `команда "\uFEFFhelp" неизвестна` | BOM в начале строки из конвейера | `TrimCommand` срезает BOM; при вводе с клавиатуры не встречается |
| `ExecuteCommand: сервер не ответил` после `exit` | сервер уже завершился | это ожидаемое поведение, подключение больше не нужно |
