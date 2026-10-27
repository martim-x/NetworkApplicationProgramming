# ЛР7 — команды сборки и запуска (мейлслоты)

Команды — из корня репозитория, в консоли с окружением MSVC.

## 1. Сборка

```cmd
cd /d "%USERPROFILE%\Documents\Visual Studio Code\NetworkApplicationProgramming"
if not exist lab7\build mkdir lab7\build
cl /nologo /EHsc /utf-8 /W3 lab7\ServerMS\ServerMS.cpp /Fe:lab7\build\ServerMS.exe
cl /nologo /EHsc /utf-8 /W3 lab7\ClientMS\ClientMS.cpp /Fe:lab7\build\ClientMS.exe
```

| Ключ | Что делает |
|---|---|
| `/EHsc` | стандартная обработка исключений C++ для `throw string` в обработчиках ошибок |
| `/utf-8` | исходник в UTF-8; без флага MSVC читает его как cp1251 и русский текст ломается |
| `/W3` | уровень предупреждений (на этом коде предупреждений нет) |
| `/Fe:` | имя выходного exe |

## 2. Сервер и клиент на одной машине

Окно 1 (сервер запускается первым — мейлслот должен существовать до записи):

```cmd
cd /d "%USERPROFILE%\Documents\Visual Studio Code\NetworkApplicationProgramming\lab7\build"
ServerMS.exe
```

Окно 2:

```cmd
cd /d "%USERPROFILE%\Documents\Visual Studio Code\NetworkApplicationProgramming\lab7\build"
ClientMS.exe \\.\mailslot\Box 10
```

Ожидаемо в окне клиента:

```
ClientMS: Connecting to mailslot \\.\mailslot\Box...
Connected to mailslot.
Sent [1]: Hello from ClientMS 001
...
Exchange time for 10 messages: 0 seconds.
```

В окне сервера — по одному `Received: Hello from ClientMS 001..010`, затем `Received STOP signal` и время обмена.

## 3. Второй клиент (один мейлслот — много писателей)

```cmd
ClientMS.exe \\.\mailslot\Box 3
```

Сервер принимает сообщения второго клиента в тот же журнал: мейлслот не различает отправителей.

## 4. Проверка отказа: мейлслота нет

```cmd
ClientMS.exe \\.\mailslot\NoBox 1
```

Ожидаемо: `CreateFile: ERROR_FILE_NOT_FOUND: Мейлслот не найден`.

## 5. Широковещательное имя (ограничение платформы)

```cmd
ClientMS.exe \\*\mailslot\Box 1
```

На Windows 11 ожидаемо: `Ошибка Mailslot, код 3025`. Расшифровка: `net helpmsg 3025` → «Remote mailslots have been deprecated». Локальное имя `\\.\mailslot\Box` работает, сетевое — нет.

## 6. Разбор ошибок

| Сообщение | Причина | Лечение |
|---|---|---|
| `CreateFile: ERROR_FILE_NOT_FOUND` | сервер не запущен или другое имя мейлслота | запустить `ServerMS.exe` первым, сверить имя |
| `Ошибка Mailslot, код 3025` | попытка удалённого/широковещательного мейлслота | использовать локальное имя `\\.\mailslot\Box` |
| `CreateFile: ERROR_INVALID_NAME` (123) | неверный формат имени | формат: `\\.\mailslot\Имя` или `\\Сервер\mailslot\Имя` |
| сервер не печатает сообщения | клиент отправил нулевую длину | `ReadFile` вернул 0 байт, цикл пропускает пустое сообщение |
| `ERROR_ACCESS_DENIED` (5) | нет прав на мейлслот | проверить ACL, запустить с нужными правами |
