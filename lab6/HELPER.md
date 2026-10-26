# ЛР6 — команды сборки и запуска (широковещательные адреса)

Все команды — из корня репозитория, в консоли с окружением MSVC (`vcvars64.bat`).

## 1. Сборка

```cmd
cd /d "%USERPROFILE%\Documents\Visual Studio Code\NetworkApplicationProgramming"
if not exist lab6\build mkdir lab6\build
cl /nologo /EHsc /utf-8 /W3 lab6\ServerB\ServerB.cpp /Fe:lab6\build\ServerB.exe
cl /nologo /EHsc /utf-8 /W3 lab6\ClientB\ClientB.cpp /Fe:lab6\build\ClientB.exe
```

| Ключ | Что делает |
|---|---|
| `/EHsc` | стандартная обработка исключений C++ — нужна для `throw string` в обработчиках ошибок |
| `/utf-8` | исходник в UTF-8; без флага MSVC читает его как cp1251 и русский текст в строках ломается |
| `/W3` | уровень предупреждений (на этом коде предупреждений нет) |
| `/Fe:` | имя выходного exe |

Дополнительно линкуется `iphlpapi.lib` (автоопределение широковещательного адреса через `GetAdaptersInfo`) — подключена в исходнике через `#pragma comment`.

## 2. Сервер и клиент на одной машине

Окно 1:

```cmd
cd /d "%USERPROFILE%\Documents\Visual Studio Code\NetworkApplicationProgramming\lab6\build"
ServerB.exe Hello 9101
```

Окно 2:

```cmd
cd /d "%USERPROFILE%\Documents\Visual Studio Code\NetworkApplicationProgramming\lab6\build"
ClientB.exe Hello 9101
```

Ожидаемый вывод клиента:

```
ClientB: широковещательный поиск сервера "Hello" на порту 9101 (адрес 192.168.31.255)
Получен отклик: "Hello"
Сервер найден: 192.168.31.80:64600
```

## 3. Явное задание широковещательного адреса

```cmd
ClientB.exe Hello 9101 255.255.255.255
ClientB.exe Hello 9101 127.255.255.255
ClientB.exe Hello 9101 192.168.31.255
```

## 4. Проверка отказов

```cmd
ClientB.exe Other 9101
ClientB.exe Hello 9102
```

Ожидаемо: `Сервер с позывным "..." не найден`.

## 5. Два сервера с одним позывным (задание 6)

Окно 1:

```cmd
ServerB.exe Hello 9101
```

Окно 2 (через пару секунд):

```cmd
ServerB.exe Hello 9101
```

Второй сервер при старте печатает:

```
ВНИМАНИЕ: в сети обнаружены серверы с позывным "Hello": 1 (запрос отправлен до привязки своего сокета)
```

## 6. Другой порт — другой сервер

```cmd
ServerB.exe Hello 9102
ClientB.exe Hello 9102
```

Порт — единственный «адрес» сервера, который клиент знает заранее; позывной уточняет, тот ли это сервер.

## 7. Разбор ошибок

| Сообщение | Причина | Лечение |
|---|---|---|
| `Ошибка: sendto: WSAEACCES` | не выставлен `SO_BROADCAST` | опция уже стоит в коде; если убрать — ошибка вернётся |
| `Сервер ... не найден` сразу после старта сервера | сервер ещё не привязал сокет (опрос сети в `CountServers`) | подождать ~1 с и повторить запрос |
| `Ошибка: bind: WSAEADDRINUSE` | порт занят другим процессом | взять другой порт (`9102`, `9103`, …) |
| сервер печатает `отклонён чужой запрос` | клиент отправил другой позывной | это ожидаемое поведение задания 1 |
| `Ошибка: socket: WSANOTINITIALISED` | не вызван `WSAStartup` | в коде вызывается в `main` до создания сокетов |
