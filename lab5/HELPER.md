# ЛР5 — команды сборки и запуска (символические имена)

Команды — из корня репозитория, в консоли с окружением MSVC.

## 1. Сборка

```cmd
cd /d "%USERPROFILE%\Documents\Visual Studio Code\NetworkApplicationProgramming"
if not exist lab5\build mkdir lab5\build
cl /nologo /EHsc /utf-8 /W3 lab5\ServerB\ServerB.cpp /Fe:lab5\build\ServerB.exe
cl /nologo /EHsc /utf-8 /W3 lab5\ClientS\ClientS.cpp /Fe:lab5\build\ClientS.exe
```

| Ключ | Что делает |
|---|---|
| `/EHsc` | стандартная обработка исключений C++ для `throw string` в обработчиках ошибок |
| `/utf-8` | исходник в UTF-8; без флага русский текст в строках ломается (MSVC читает как cp1251) |
| `/W3` | уровень предупреждений (на этом коде предупреждений нет) |
| `/Fe:` | имя выходного exe |

## 2. Сервер и клиент на одной машине

Окно 1:

```cmd
cd /d "%USERPROFILE%\Documents\Visual Studio Code\NetworkApplicationProgramming\lab5\build"
ServerB.exe Hello 9103
```

Окно 2:

```cmd
hostname
```

```cmd
ClientS.exe ИМЯ_КОМПЬЮТЕРА Hello 9103
```

Ожидаемый вывод клиента:

```
ClientS: поиск сервера "Hello" на компьютере "WIN11-DESKTOP-T", порт 9103
Имя "WIN11-DESKTOP-T" разрешено в адрес 192.168.31.80
Получен отклик: "Hello"
Сервер найден: 192.168.31.80:55507
```

## 3. Без аргументов — по своему имени

```cmd
ClientS.exe
```

Клиент берёт имя своего компьютера через `gethostname`, позывной `Hello`, порт `2000`.

## 4. Проверка отказов

```cmd
ClientS.exe localhost Hello 9103
ClientS.exe no-such-host-xyz Hello 9103
ClientS.exe ИМЯ_КОМПЬЮТЕРА Other 9103
```

Ожидаемо: первая команда находит сервер через `127.0.0.1`; вторая — `WSAHOST_NOT_FOUND: Хост не найден`; третья — имя разрешается, но отклик не совпадает с позывным.

## 5. Что смотреть в журнале сервера

```cmd
ServerB.exe Hello 9103
```

```
ServerB: символическое имя компьютера: Win11-Desktop-Tim
Запрос от клиента 192.168.31.80:55506 (имя: host.docker.internal)
Клиенту отправлен позывной "Hello"
```

`gethostname` даёт имя узла, `gethostbyaddr` — то, что вернула служба разрешения имён для адреса клиента (может отличаться от имени компьютера).

## 6. Разбор ошибок

| Сообщение | Причина | Лечение |
|---|---|---|
| `WSAHOST_NOT_FOUND` | имя не разрешается (нет записи в DNS/`hosts`) | указать имя из `hostname` или `localhost` |
| `WSATRY_AGAIN` | временный сбой службы имён | повторить запрос |
| `Сервер ... не найден` при верном имени | сервер не запущен, другой порт или другой позывной | сверить порт и позывной с аргументами `ServerB` |
| `Ошибка: sendto` | адрес не разрешён (нулевой) | проверить имя: при пустом `h_addr_list[0]` функция вернёт `false` до отправки |
| `Ошибка: socket: WSANOTINITIALISED` | нет `WSAStartup` | в коде он вызывается первым |
