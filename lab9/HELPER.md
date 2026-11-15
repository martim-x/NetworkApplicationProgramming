# ЛР9 — команды сборки и запуска (клиент параллельного сервера)

## 1. Сборка

```cmd
cd /d "%USERPROFILE%\Documents\Visual Studio Code\NetworkApplicationProgramming"
if not exist lab9\build mkdir lab9\build
cl /nologo /EHsc /utf-8 /W3 lab9\ClientEcho\ClientEcho.cpp lab9\ClientApi\ClientApi.cpp /Fe:lab9\build\ClientEcho.exe
cl /nologo /EHsc /utf-8 /W3 lab9\ClientTime\ClientTime.cpp lab9\ClientApi\ClientApi.cpp /Fe:lab9\build\ClientTime.exe
cl /nologo /EHsc /utf-8 /W3 lab9\ClientRand\ClientRand.cpp lab9\ClientApi\ClientApi.cpp /Fe:lab9\build\ClientRand.exe
cl /nologo /EHsc /utf-8 /W3 lab9\tests\ClientApiTests.cpp /Fe:lab9\build\ClientApiTests.exe
```

| Ключ | Что делает |
|---|---|
| `/EHsc` | стандартная обработка исключений C++ |
| `/utf-8` | исходники в UTF-8 (иначе русский текст читается как cp1251) |
| `/W3` | уровень предупреждений |
| `/Fe:` | имя выходного exe |

`ClientApi.cpp` компилируется в каждый клиент — отдельной библиотеки в этой лабе нет.

## 2. Запуск сервера (из ЛР8)

```cmd
cd /d "%USERPROFILE%\Documents\Visual Studio Code\NetworkApplicationProgramming\lab8\build"
ConcurrentServer.exe 9301 ServiceLibrary.dll \\.\pipe\TestConsole 9300 TestServer
```

Сервер должен быть запущен **до** клиентов: клиенты ищут его широковещанием.

## 3. Запуск клиентов

```cmd
cd /d "%USERPROFILE%\Documents\Visual Studio Code\NetworkApplicationProgramming\lab9\build"
ClientEcho.exe TestServer 9300 9301 5 50
ClientTime.exe TestServer 9300 9301 3
ClientRand.exe TestServer 9300 9301 3
ClientEcho.exe --dns localhost 9301 5 50
ClientTime.exe --dns localhost 9301 3
ClientRand.exe --dns localhost 9301 3
```

Аргументы широковещательного режима: `[позывной] [UDP-порт поиска] [TCP-порт сервера] [количество] [задержка мс]`.
Режим DNS: `--dns <hostname> <tcpPort> [count] [delayMs]` — имя разрешается через `getaddrinfo` (IPv4) и клиент подключается по TCP; hostname при этом не используется как UDP-позывной.

Ожидаемый вывод `ClientEcho`:

```
ClientEcho: поиск сервера "TestServer" (широковещание, UDP-порт 9300, TCP-порт 9301)
ClientEcho: найден сервер 192.168.31.80:9301
  эхо: Hello from ClientEcho 001
  ...
ClientEcho: корректных ответов 5 из 5, время сеанса 0.328 с
ClientEcho: ответ на некорректный запрос: ErrorInquiry
```

## 4. Три клиента одновременно

```cmd
start ClientEcho.exe TestServer 9300 9301 4
start ClientTime.exe TestServer 9300 9301 4
start ClientRand.exe TestServer 9300 9301 4
```

В журнале сервера будет видно три параллельных сеанса (`DispatchServer: запрос ...`, затем `APC ... завершение`).

## 5. Проверка отказа поиска

```cmd
ClientEcho.exe NoSuchServer 9300 9301 2 10
```

Ожидаемо: `ClientEcho: сервер не найден` (широковещательный запрос ушёл, никто не откликнулся).

## 6. Разбор ошибок

| Сообщение | Причина | Лечение |
|---|---|---|
| `сервер не найден` | сервер не запущен, другой позывной/UDP-порт, или брандмауэр блокирует широковещание | сверить аргументы с `ConcurrentServer`, проверить порт |
| `ConnectToServer: WSAECONNREFUSED` | сервер найден, но TCP-порт не тот | указать третий аргумент ровно как TCP-порт сервера |
| `ConnectToServer: WSAETIMEDOUT` | сервер не отвечает (например, после команды `stop`) | выполнить команду `start` через консоль сервера |
| `ответ на некорректный запрос` пустой | сервер закрыл соединение без ответа | убедиться, что запущена актуальная сборка ЛР8 |
| `ClientEcho: корректных ответов 4 из 5` | потеря первого сообщения при старом сервере | пересобрать `ServiceLibrary.dll` (в актуальной версии остаток буфера передаётся обслуживающему потоку) |
| нет ответа на `time`/`rand` | клиент отправил строку, отличную от кода запроса | отправлять ровно `time` / `rand` |
| `некорректный TCP-порт/UDP-порт/количество/задержка` (код возврата 2) | аргумент не целое число или вне диапазона (порты 1..65535, количество >= 1, задержка >= 0) | исправить аргумент; `atoi`-тихие ошибки («12abc» -> 12) больше не пропускаются |
| `не удалось разрешить имя ... или подключиться к серверу` | DNS не разрешил hostname (код 11001/11004) или ни один адрес не принял TCP за 3 с | проверить имя хоста и доступность сервера; код возврата 1 |
| `корректных ответов X из N` + код возврата 1 | сервер оборвал сеанс или ответил частично — получено меньше, чем запрошено | проверять журнал сервера; частичный rand-ответ (менее 4 байт) тоже считается отказом |
| выход с кодом 1 после `сервер не найден` | широковещательный поиск не получил отклик за 3 с | запустить сервер до клиентов, сверить позывной и UDP-порт |
