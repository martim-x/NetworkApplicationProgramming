# ЛР6 — команды для сборки и запуска (Named Pipe)

Сборка выполняется в консоли разработчика Visual Studio (`x64 Native Tools Command Prompt for VS`) или через `vcvars64.bat`. Все команды — из корня репозитория.

## 1. Сборка всех четырёх приложений

```cmd
cd /d "%USERPROFILE%\Documents\Visual Studio Code\NetworkApplicationProgramming"
if not exist lab6\build mkdir lab6\build
cl /nologo /EHsc /utf-8 /W3 lab6\ServerNP\ServerNP.cpp /Fe:lab6\build\ServerNP.exe
cl /nologo /EHsc /utf-8 /W3 lab6\ClientNP\ClientNP.cpp /Fe:lab6\build\ClientNP.exe
cl /nologo /EHsc /utf-8 /W3 lab6\ClientNPt\ClientNPt.cpp /Fe:lab6\build\ClientNPt.exe
cl /nologo /EHsc /utf-8 /W3 lab6\ClientNPct\ClientNPct.cpp /Fe:lab6\build\ClientNPct.exe
```

| Ключ | Что делает |
|---|---|
| `/EHsc` | включает стандартную обработку исключений C++ — без неё `throw string` в наших обработчиках ошибок не разворачивает стек корректно |
| `/utf-8` | исходники в UTF-8 без BOM: без флага MSVC читает их как ANSI (cp1251) и русский текст в строках превращается в мусор |
| `/W3` | уровень предупреждений; на этом коде предупреждений нет |
| `/Fe:` | имя выходного exe; объектные файлы кладутся рядом |
| `/nologo` | убирает баннер компилятора из вывода |

## 2. Запуск: сервер и клиент на одном компьютере

Окно 1:

```cmd
cd /d "%USERPROFILE%\Documents\Visual Studio Code\NetworkApplicationProgramming\lab6\build"
ServerNP.exe \\.\pipe\Tube
```

Окно 2:

```cmd
cd /d "%USERPROFILE%\Documents\Visual Studio Code\NetworkApplicationProgramming\lab6\build"
ClientNP.exe \\.\pipe\Tube 3
```

Ожидаемый вывод клиента (время обмена зависит от машины, порядок миллисекунд):

```
ClientNP: подключение к каналу \\.\pipe\Tube
Соединение с сервером установлено
Получено: Hello from ClientNP 001
Получено: Hello from ClientNP 002
Получено: Hello from ClientNP 003
Время обмена 3 сообщениями: 0.003 с
```

## 3. Два сервера на разных каналах (аналог разных портов)

Окно 1 и окно 2 — по серверу на каждое имя канала:

```cmd
ServerNP.exe \\.\pipe\Test1
```

```cmd
ServerNP.exe \\.\pipe\Test2
```

Окно 3 — клиент обращается к первому каналу, второй продолжает ждать:

```cmd
ClientNP.exe \\.\pipe\Test1 2
```

## 4. Проверка обработки ошибок: клиент без сервера

```cmd
ClientNP.exe \\.\pipe\NoSuchPipe 1
```

Ожидаемо:

```
Ошибка: CreateFile: ERROR_FILE_NOT_FOUND: Канал с таким именем не создан
```

## 5. Транзакционные клиенты (задания 7 и 8)

Сервер уже запущен (`\\.\pipe\Test1`):

```cmd
ClientNPt.exe \\.\pipe\Test1 3
```

```cmd
ClientNPct.exe \\.\pipe\Test1 2
```

`ClientNPt` держит одно соединение и делает 3 транзакции через `TransactNamedPipe`; `ClientNPct` делает 2 вызова `CallNamedPipe`, и в журнале сервера это два отдельных сеанса по одному сообщению.

## 6. Сетевое имя канала

```cmd
hostname
ClientNP.exe \\ИМЯ_КОМПЬЮТЕРА\pipe\Test1 2
```

| Параметр | Что означает |
|---|---|
| `\\.\pipe\Tube` | локальный формат: точка — текущий компьютер |
| `\\ИМЯ_КОМПЬЮТЕРА\pipe\Tube` | сетевой формат: канал на другом компьютере через SMB |
| `Tube` | короткое имя, программа сама подставит `\\.\pipe\` |
| второй аргумент | количество сообщений (транзакций); без него число спрашивается с консоли |

## 7. Замер на 1000 сообщениях

```cmd
ServerNP.exe \\.\pipe\Speed
ClientNP.exe \\.\pipe\Speed 1000
```

Фактический результат на этой машине: клиент `0.083 с`, сервер `0.082 с`, все 1000 сообщений получены и возвращены.

## 8. Разбор ошибок

| Сообщение | Причина | Лечение |
|---|---|---|
| `Ошибка: CreateFile: ERROR_FILE_NOT_FOUND` | сервер не запущен или другое имя канала | запустить `ServerNP.exe` с тем же именем |
| `Ошибка: CreateFile: ERROR_PIPE_BUSY` после ожидания | экземпляр занят дольше 5000 мс | увеличить таймаут `WaitNamedPipe` или запускать клиентов по очереди |
| `Ошибка: TransactNamedPipe: Ошибка Named Pipe, код 230` | дескриптор клиента не переведён в режим чтения сообщениями | вызывать `SetNamedPipeHandleState` с `PIPE_READMODE_MESSAGE` (в `ClientNPt` уже сделано) |
| `cl` не найден | консоль без окружения MSVC | запустить `vcvars64.bat` или «x64 Native Tools Command Prompt for VS» |
| русский текст в выводе — мусор | исходник прочитан как ANSI | собирать с `/utf-8` |
| `LNK2019: unresolved external symbol ... WSAStartup` | не подключена библиотека Winsock | ключ `/link WS2_32.lib` или `#pragma comment(lib, "WS2_32.lib")` (в исходниках есть) |
