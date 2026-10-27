# ЛР8 — команды сборки и запуска (параллельный сервер)

Команды — из корня репозитория, в консоли с окружением MSVC.

## 1. Сборка библиотеки и сервера

```cmd
cd /d "%USERPROFILE%\Documents\Visual Studio Code\NetworkApplicationProgramming"
if not exist lab8\build mkdir lab8\build
cl /nologo /EHsc /utf-8 /W3 /LD lab8\ServiceLibrary\ServiceLibrary.cpp /Fe:lab8\build\ServiceLibrary.dll
cl /nologo /EHsc /utf-8 /W3 lab8\ConcurrentServer\ConcurrentServer.cpp /Fe:lab8\build\ConcurrentServer.exe
cl /nologo /EHsc /utf-8 /W3 lab8\ClientTy\ClientTy.cpp /Fe:lab8\build\ClientTy.exe
```

| Ключ | Что делает |
|---|---|
| `/LD` | собрать DLL вместо exe (создаёт `ServiceLibrary.dll`, `.lib`, `.exp`) |
| `/EHsc` | стандартная обработка исключений C++ |
| `/utf-8` | исходники в UTF-8; без флага русский текст в строках ломается |
| `/W3` | уровень предупреждений (на этом коде предупреждений нет) |
| `/Fe:` | имя выходного файла |

Сервер загружает DLL по имени во время работы (`LoadLibrary`), поэтому импорт-библиотека не нужна: достаточно, чтобы `ServiceLibrary.dll` лежала рядом с exe (или был указан путь).

## 2. Запуск сервера

```cmd
cd /d "%USERPROFILE%\Documents\Visual Studio Code\NetworkApplicationProgramming\lab8\build"
ConcurrentServer.exe 9301 ServiceLibrary.dll \\.\pipe\TestConsole 9300 TestServer
```

Аргументы: `[TCP-порт] [DLL] [имя канала] [UDP-порт] [позывной] [лимит обслуживания мс]`. По умолчанию — `2000 ServiceLibrary.dll \\.\pipe\ConcurrentServer 9200 ConcurrentServer 60000`.

Ожидаемый вывод:

```
ConcurrentServer: TCP-порт 9301, UDP-порт 9300, канал управления \\.\pipe\TestConsole, позывной "TestServer"
main: библиотека ServiceLibrary.dll загружена, SSS и FindService импортированы
ConsolePipe: канал управления создан: \\.\pipe\TestConsole
ResponseServer: приём широковещательных запросов на UDP-порт 9300, позывной "TestServer"
AcceptServer: прослушивание TCP-порта 9301
```

## 3. Проверка запросов без отдельного клиента (PowerShell)

```powershell
$c = New-Object Net.Sockets.TcpClient('127.0.0.1', 9301)
$s = $c.GetStream()
$b = [Text.Encoding]::UTF8.GetBytes("echo`0"); $s.Write($b,0,$b.Length); $s.Flush()
$b = [Text.Encoding]::UTF8.GetBytes("Hello from Client 001`0"); $s.Write($b,0,$b.Length); $s.Flush()
$buf = New-Object byte[] 256
$s.Read($buf,0,256) | Out-Null
[Text.Encoding]::UTF8.GetString($buf).TrimEnd([char]0)
$c.Close()
```

```powershell
$c = New-Object Net.Sockets.TcpClient('127.0.0.1', 9301)
$s = $c.GetStream()
$b = [Text.Encoding]::UTF8.GetBytes("time`0"); $s.Write($b,0,$b.Length); $s.Flush()
$buf = New-Object byte[] 64
$s.Read($buf,0,64) | Out-Null
[Text.Encoding]::UTF8.GetString($buf).TrimEnd([char]0)
$c.Close()
```

```powershell
$c = New-Object Net.Sockets.TcpClient('127.0.0.1', 9301)
$s = $c.GetStream()
$b = [Text.Encoding]::UTF8.GetBytes("rand`0"); $s.Write($b,0,$b.Length); $s.Flush()
$buf = New-Object byte[] 4
$s.Read($buf,0,4) | Out-Null
[BitConverter]::ToInt32($buf, 0)
$c.Close()
```

## 4. Команды консоли через именованный канал

```powershell
function Send-ConsoleCommand($name, $cmd) {
    $pipe = New-Object System.IO.Pipes.NamedPipeClientStream('.', $name, [System.IO.Pipes.PipeDirection]::InOut)
    $pipe.Connect(3000)
    $b = [Text.Encoding]::UTF8.GetBytes($cmd)
    $pipe.Write($b, 0, $b.Length); $pipe.Flush()
    $buf = New-Object byte[] 128
    $task = $pipe.ReadAsync($buf, 0, 128)
    if ($task.Wait(3000) -and $task.Result -gt 0) {
        [Text.Encoding]::UTF8.GetString($buf, 0, $task.Result).TrimEnd([char]0)
    } else { '<нет ответа>' }
    $pipe.Dispose()
}

Send-ConsoleCommand 'TestConsole' 'statistics'
Send-ConsoleCommand 'TestConsole' 'stop'
Send-ConsoleCommand 'TestConsole' 'start'
Send-ConsoleCommand 'TestConsole' 'shutdown'
```

Ожидаемо: `statistics`/`stop`/`start`/`shutdown` возвращаются теми же строками, неизвестная команда — `nocmd`.

## 5. Поиск сервера широковещанием (ResponseServer)

```cmd
cd /d "%USERPROFILE%\Documents\Visual Studio Code\NetworkApplicationProgramming\lab4\build"
ClientB.exe TestServer 9300
```

Ожидаемо: `Сервер найден: <IP>:9300`.

## 5a. Watchdog: лимит времени обслуживания (задание 20)

```cmd
ConcurrentServer.exe 9301 ServiceLibrary.dll \\.\pipe\TestConsole 9300 TestServer 3000
```

| Параметр | Что означает |
|---|---|
| 6-й аргумент | лимит обслуживания одного клиента, мс; по умолчанию 60 000 |
| `CreateWaitableTimer` + `SetWaitableTimer` | ожидающий таймер сеанса, взводится на лимит |
| `WaitForMultipleObjects` в потоке `Watchdog` | ожидание набора таймеров активных сеансов |
| `TerminateThread` | принудительное завершение зациклившегося потока (как требует задание) |
| `CancelWaitableTimer` в `GarbageCleaner` | отмена таймера при нормальном завершении |

Проверка: подключиться, отправить `echo` и замолчать дольше лимита. В журнале:

```
Watchdog: клиент 127.0.0.1:59781 превысил лимит 3000 мс — поток завершается принудительно
```

Слот освобождается, следующий клиент обслуживается нормально.

## 5b. ClientTy: нагрузка подключениями (задание 17)

```cmd
ClientTy.exe 127.0.0.1 9301 20 50
```

Аргументы: `[IP] [порт] [циклов] [задержка мс]`. Ожидаемо: `успешных циклов 20 из 20, сбоев 0, время 1.516 с` — в журнале сервера столько же сеансов `APC … завершение обслуживания`.

## 6. Разбор ошибок

| Сообщение | Причина | Лечение |
|---|---|---|
| `main: LoadLibrary ServiceLibrary.dll: ERROR_FILE_NOT_FOUND` | DLL не собрана или лежит в другом каталоге | собрать `/LD` и положить рядом с exe, либо указать полный путь вторым аргументом |
| `main: в библиотеке не найдены экспортируемые функции SSS/FindService` | собрана другая DLL | проверить `dumpbin /exports ServiceLibrary.dll` |
| `AcceptServer: bind: WSAEADDRINUSE` | порт занят | взять другой TCP-порт |
| `console: <нет ответа>` | клиент не вычитал ответ до разрыва канала | читать ответ асинхронно или до `FlushFileBuffers` сервера |
| клиент получает `ErrorInquiry` | первое сообщение не `echo`/`time`/`rand` | отправить корректный код запроса |
| сеанс `echo` не завершается | клиент не закрыл сокет и не отправил пустое сообщение | закрыть сокет или отправить `\0` |
| сервер не реагирует на `stop` | команда отправлена не в тот канал | имя канала — третий аргумент `ConcurrentServer` |
| `Watchdog: … превысил лимит … поток завершается принудительно` | обслуживание клиента дольше 6-го аргумента | увеличить лимит или разобраться, почему клиент молчит |
| `ClientTy: connect: WSAECONNREFUSED` | сервер не запущен или другой порт | запустить сервер, сверить второй аргумент |
