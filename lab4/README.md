# Лабораторная работа №4: Именованные каналы (Named Pipes)

## 1. Теория: Именованные каналы в Windows

**Именованный канал (Named Pipe)** — это механизм межпроцессного взаимодействия (IPC), обеспечивающий обмен данными между процессами на одном компьютере или в локальной сети. В отличие от анонимных каналов, именованные каналы имеют имя в файловой системе и могут использоваться несвязанными процессами.

### Ключевые особенности:
- **Двунаправленное общение** (full-duplex) — `PIPE_ACCESS_DUPLEX`
- **Режим сообщений** (`PIPE_TYPE_MESSAGE`) — границы сообщений сохраняются
- **Режим байтов** (`PIPE_TYPE_BYTE`) — поток байтов как в TCP
- **Синхронный/асинхронный** режимы работы
- **Работа в сети** — имя вида `\\Server\pipe\Name` позволяет подключаться к удалённому серверу

### Основные API функции:
| Функция | Назначение |
|---------|------------|
| `CreateNamedPipe` | Создание серверного экземпляра канала |
| `ConnectNamedPipe` | Ожидание подключения клиента |
| `CreateFile` | Подключение клиента к существующему каналу |
| `ReadFile` / `WriteFile` | Чтение/запись данных |
| `DisconnectNamedPipe` | Разрыв соединения с текущим клиентом |
| `CloseHandle` | Закрытие дескриптора канала |
| `SetNamedPipeHandleState` | Изменение режима (сообщения/байты, блокирующий/неблокирующий) |

### Схема взаимодействия (Message-mode, Duplex):
```
Сервер                          Клиент
------                          ------
CreateNamedPipe("\\.\pipe\Tube")
       |
       v
ConnectNamedPipe()  <---- блокируется до подключения
       |
       v                    CreateFile("\\.\pipe\Tube")
       |                         |
       v                         v
ReadFile()  <---------------  WriteFile()
       |                         |
       v                         v
WriteFile() -------------->  ReadFile()
       |                         |
       v                         v
DisconnectNamedPipe()      CloseHandle()
```

---

## 2. Структура Named Pipe-сервера

```cpp
// 1. Создание именованного канала
HANDLE hPipe = CreateNamedPipeA(
    "\\\\.\\pipe\\Tube",           // имя канала
    PIPE_ACCESS_DUPLEX,            // двунаправленный
    PIPE_TYPE_MESSAGE |            // режим сообщений
    PIPE_READMODE_MESSAGE |        // чтение сообщениями
    PIPE_WAIT,                     // блокирующий режим
    PIPE_UNLIMITED_INSTANCES,      // макс. экземпляров
    512,                           // выходной буфер
    512,                           // входной буфер
    INFINITE,                      // таймаут ожидания клиента
    NULL                           // атрибуты безопасности
);

// 2. Ожидание подключения клиента
ConnectNamedPipe(hPipe, NULL);  // блокируется до подключения

// 3. Цикл обмена данными
while (true) {
    DWORD bytesRead;
    char buffer[512];
    
    // Чтение сообщения от клиента
    ReadFile(hPipe, buffer, sizeof(buffer)-1, &bytesRead, NULL);
    if (bytesRead == 0 || buffer[0] == '\0') break;  // конец сессии
    
    buffer[bytesRead] = '\0';
    cout << "Received: " << buffer << endl;
    
    // Эхо-ответ
    DWORD bytesWritten;
    WriteFile(hPipe, buffer, bytesRead, &bytesWritten, NULL);
}

// 4. Разрыв соединения и ожидание следующего клиента
DisconnectNamedPipe(hPipe);
// повторить ConnectNamedPipe для нового клиента
```

**Важно:** Один дескриптор `hPipe` обслуживает **последовательно** множество клиентов. Для параллельного обслуживания нужно создавать отдельные экземпляры канала (через `PIPE_UNLIMITED_INSTANCES`) и использовать потоки/IOCP.

---

## 3. Структура Named Pipe-клиента

```cpp
// 1. Подключение к существующему каналу
HANDLE hPipe = CreateFileA(
    "\\\\.\\pipe\\Tube",           // имя канала (\\Server\pipe\Name для сети)
    GENERIC_READ | GENERIC_WRITE,  // доступ на чтение и запись
    FILE_SHARE_READ | FILE_SHARE_WRITE,
    NULL,
    OPEN_EXISTING,                 // открыть существующий
    0,
    NULL
);

// 2. Цикл обмена данными
for (int i = 0; i < messageTotal; i++) {
    sprintf(sendBuffer, "Hello from Client %03d", i);
    
    // Отправка
    DWORD bytesWritten;
    WriteFile(hPipe, sendBuffer, strlen(sendBuffer)+1, &bytesWritten, NULL);
    
    // Получение ответа
    DWORD bytesRead;
    ReadFile(hPipe, receiveBuffer, sizeof(receiveBuffer)-1, &bytesRead, NULL);
    receiveBuffer[bytesRead] = '\0';
    cout << "Received: " << receiveBuffer << endl;
}

// 3. Завершение
WriteFile(hPipe, "", 1, &bytesWritten, NULL);  // сигнал конца
CloseHandle(hPipe);
```

---

## 4. Отличие Named Pipes от сокетов (TCP/UDP)

| Характеристика | Named Pipes | TCP Sockets | UDP Sockets |
|---|---|---|---|
| **Область действия** | Локальная машина / сеть (SMB) | Сеть (IP) | Сеть (IP) |
| **Адресация** | Имя `\\.\pipe\Name` | IP + порт | IP + порт |
| **Надёжность** | Гарантированная доставка, порядок | Гарантированная доставка, порядок | Нет гарантий |
| **Границы сообщений** | Да (в режиме `PIPE_TYPE_MESSAGE`) | Нет (поток байтов) | Да (датаграммы) |
| **Установка соединения** | `ConnectNamedPipe` / `CreateFile` | `connect` / `accept` | Не требуется |
| **Параллельные клиенты** | Несколько экземпляров канала | `accept` + потоки/epoll | Один сокет для всех |
| **Безопасность** | ACL, дескрипторы безопасности | TLS/SSL, firewall | DTLS, firewall |
| **Производительность** | Высокая (локально, zero-copy возможен) | Хорошая | Очень высокая (малые пакеты) |

---

## 5. Когда выбирать Named Pipes

| Сценарий | Причина |
|---|---|
| **IPC на одной машине** | Проще сокетов, нет сетевого стека, высокая скорость |
| **Межпроцессное взаимодействие сервисов Windows** | Нативный механизм, интеграция с безопасностью Windows |
| **Обмен структурированными сообщениями** | Режим сообщений сохраняет границы |
| **Взаимодействие с legacy-приложениями Windows** | Стандартный API Win32 |
| **Требуется имитация файлового интерфейса** | `ReadFile`/`WriteFile` как для файлов |

**Не выбирать, если:** нужна кроссплатформенность (Linux/macOS), высокая нагрузка в сети (лучше TCP/gRPC), или архитектура микросервисов.

---

## 6. Безопасность Named Pipes

- **ACL (Access Control Lists)** — через `SECURITY_ATTRIBUTES` в `CreateNamedPipe`
- **Именование** — `\\.\pipe\` (локально) или `\\Server\pipe\` (в сети через SMB)
- **Использование `FILE_FLAG_FIRST_PIPE_INSTANCE`** — защита от скватинга имени
- **Проверка клиента** — `GetNamedPipeClientProcessId`, `GetNamedPipeClientComputerName`

---

## 7. Демонстрация работы (из кода лабы)

### Сервер (ServerNP.cpp):
- Создаёт канал `\\.\pipe\Tube` в режиме сообщений, дуплекс
- Последовательно обслуживает клиентов через `ConnectNamedPipe` / `DisconnectNamedPipe`
- Эхо-ответ на каждое сообщение
- Подсчёт сообщений и времени обмена для каждого клиента

### Клиент (ClientNP.cpp):
- Подключается к `\\.\pipe\Tube` через `CreateFile`
- Отправляет заданное число сообщений (`Hello from ClientNP XXX`)
- Получает эхо-ответы, измеряет общее время обмена
- Отправляет пустое сообщение как сигнал завершения сессии

### Запуск:
```cmd
# Терминал 1 — сервер
ServerNP.exe

# Терминал 2 — клиент (10 сообщений)
ClientNP.exe \\.\pipe\Tube 10
# или с аргументами: ClientNP.exe [pipe_name] [port_ignored]
```

---

## 8. Типичные ошибки и их коды

| Код ошибки | Значение | Решение |
|---|---|---|
| `ERROR_PIPE_BUSY` (231) | Все экземпляры заняты | Увеличить `PIPE_UNLIMITED_INSTANCES` или подождать |
| `ERROR_FILE_NOT_FOUND` (2) | Канал не существует | Запустить сервер первым |
| `ERROR_ACCESS_DENIED` (5) | Нет прав доступа | Проверить ACL / запуск от админа |
| `ERROR_BROKEN_PIPE` (109) | Клиент отключился некорректно | Обрабатывать в сервере, пересоздавать соединение |
| `ERROR_NO_DATA` (232) | Клиент закрыл канал | Нормальное завершение сессии |
| `ERROR_PIPE_CONNECTED` (535) | Клиент уже подключён | Нормально для `ConnectNamedPipe` при возврате `FALSE` |

---

## 9. Вопросы для самопроверки

1. В чём разница между `PIPE_TYPE_MESSAGE` и `PIPE_TYPE_BYTE`?
2. Как сервер может обслуживать несколько клиентов **параллельно**?
3. Что происходит, если клиент вызывает `CreateFile` до запуска сервера?
4. Как передать дескриптор канала другому процессу?
5. В чём разница между `\\.\pipe\Name` и `\\Server\pipe\Name`?
6. Как работает `DisconnectNamedPipe` и зачем он нужен?
7. Какие преимущества Named Pipes перед TCP на localhost?

---

## 10. Полезные ссылки

- [Microsoft Docs: Named Pipes](https://learn.microsoft.com/en-us/windows/win32/ipc/named-pipes)
- [CreateNamedPipe function](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-createnamedpipea)
- [Named Pipe Security](https://learn.microsoft.com/en-us/windows/win32/ipc/named-pipe-security)
- [Named Pipe Client](https://learn.microsoft.com/en-us/windows/win32/ipc/named-pipe-client)

---

*Лабораторная работа основана на Главе 4 пособия: «Интерфейс Named Pipe».*