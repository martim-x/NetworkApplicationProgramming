// ЛР9 — автоматические тесты клиентского API (Task 2, fix/lab8-10-stability)
// Самодостаточный exe: включает ClientApi.cpp напрямую, поэтому собирается одной командой cl.
// Тестирует: точный текстовый фрейминг (NUL), склейку частичных recv, коалесцированные фреймы,
// SendAll при маленьком SO_SNDBUF, 4-байтовый rand (в т.ч. частичный), таймаут приёма,
// режим --dns через CLI реальных клиентов, сохранение широковещательного режима,
// отказ при обрыве сервера и при некорректных аргументах.
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#include "../ClientApi/ClientApi.cpp"
#include <thread>
#include <atomic>
#include <vector>
#include <string>
#include <cstdio>
#include <cstring>
#pragma comment(lib, "WS2_32.lib")
using namespace std;

static int g_pass = 0;
static int g_fail = 0;

static void Check(bool ok, const char* name, const string& detail = "")
{
	if (ok)
	{
		printf("[PASS] %s\n", name);
		g_pass++;
	}
	else
	{
		printf("[FAIL] %s%s%s\n", name, detail.empty() ? "" : " — ", detail.c_str());
		g_fail++;
	}
}

// ---------- Испытательный TCP-пир (stub-сервер) ----------

struct StubOptions
{
	int replySplitParts = 1;   // >1: ответ шлётся N кусками с паузами (проверка склейки)
	int coalesceTarget = 0;    // >0: копить ответы, потом ОДНИМ send() отправить все
	bool partialRand = false;  // rand: только 2 байта из 4, затем закрыть
	bool closeAfterFirstReply = false; // после первого ответа закрыть соединение
	bool silent = false;       // принимать и молчать (таймаут приёма у клиента)
	bool sendUnframedReply = false; // ответ без NUL (1.5 МБ) — oversized/EOF-mid-frame
	int readChunkSize = 8192;  // размер recv на стороне пира
};

static bool StubSendAll(SOCKET s, const char* data, int bytes)
{
	int sent = 0;
	while (sent < bytes)
	{
		int n = send(s, data + sent, bytes - sent, 0);
		if (n == SOCKET_ERROR || n == 0)
			return false;
		sent += n;
	}
	return true;
}

class StubServer
{
public:
	StubOptions opt;
	int port = 0;
	atomic<int> connectionsSeen{ 0 };

	void Start()
	{
		listenSocket = socket(AF_INET, SOCK_STREAM, 0);
		SOCKADDR_IN addr;
		memset(&addr, 0, sizeof(addr));
		addr.sin_family = AF_INET;
		addr.sin_addr.s_addr = htonl(INADDR_ANY);
		addr.sin_port = 0;
		if (bind(listenSocket, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR)
		{
			printf("stub: bind failed\n");
			return;
		}
		int len = sizeof(addr);
		getsockname(listenSocket, (sockaddr*)&addr, &len);
		port = ntohs(addr.sin_port);
		listen(listenSocket, 8);
		stopFlag = false;
		acceptThread = thread([this] { AcceptLoop(); });
	}

	~StubServer()
	{
		Stop();
	}

	void Stop()
	{
		if (stopFlag.exchange(true))
			return;
		if (listenSocket != INVALID_SOCKET)
		{
			closesocket(listenSocket);
			listenSocket = INVALID_SOCKET;
		}
		if (acceptThread.joinable())
			acceptThread.join();
	}

private:
	SOCKET listenSocket = INVALID_SOCKET;
	thread acceptThread;
	atomic<bool> stopFlag{ false };

	void AcceptLoop()
	{
		while (!stopFlag)
		{
			SOCKET c = accept(listenSocket, NULL, NULL);
			if (c == INVALID_SOCKET)
				break;
			connectionsSeen++;
			thread(&StubServer::HandleConnection, this, c).detach();
		}
	}

	void HandleConnection(SOCKET c)
	{
		DWORD rcvTimeout = 2000;
		setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, (const char*)&rcvTimeout, sizeof(rcvTimeout));

		string in;
		vector<string> held;      // копилка для coalesceTarget
		bool echoMode = false;
		bool closed = false;
		int repliesSent = 0;
		vector<char> buf(opt.readChunkSize);

		while (!closed)
		{
			size_t nul;
			while ((nul = in.find('\0')) != string::npos && !closed)
			{
				string frame = in.substr(0, nul);
				in.erase(0, nul + 1);
				closed = HandleFrame(c, frame, echoMode, held, repliesSent);
			}
			if (closed)
				break;
			int bytes = recv(c, buf.data(), (int)buf.size(), 0);
			if (bytes <= 0)
				break;
			in.append(buf.data(), bytes);
		}

		Sleep(30);
		closesocket(c);
	}

	bool HandleFrame(SOCKET c, const string& frame, bool& echoMode, vector<string>& held, int& repliesSent)
	{
		if (frame == "echo")
		{
			echoMode = true;
			return false;
		}

		if (echoMode)
		{
			if (opt.silent)
				return false;

			if (opt.sendUnframedReply)
			{
				string junk(1500 * 1024, 'a');
				StubSendAll(c, junk.data(), (int)junk.size());
				Sleep(50);
				shutdown(c, SD_SEND);
				return true;
			}

			if (opt.coalesceTarget > 0)
			{
				held.push_back(frame);
				if ((int)held.size() >= opt.coalesceTarget)
				{
					string combined;
					for (size_t i = 0; i < held.size(); i++)
					{
						combined += held[i];
						combined += '\0';
					}
					StubSendAll(c, combined.data(), (int)combined.size());
					held.clear();
				}
				return false;
			}

			SendReplyFramed(c, frame);
			repliesSent++;
			if (opt.closeAfterFirstReply)
			{
				Sleep(30);
				shutdown(c, SD_SEND);
				return true;
			}
			return false;
		}

		if (frame == "time")
		{
			SendReplyFramed(c, "15.11.2026/12:34:56");
			return false;
		}

		if (frame == "rand")
		{
			if (opt.partialRand)
			{
				char half[2] = { 1, 2 };
				send(c, half, 2, 0);
				Sleep(30);
				shutdown(c, SD_SEND);
				return true;
			}
			int value = 123456;
			send(c, (const char*)&value, (int)sizeof(value), 0);
			return false;
		}

		// некорректный запрос — как сервер ЛР8
		SendReplyFramed(c, "ErrorInquiry");
		return true;
	}

	void SendReplyFramed(SOCKET c, const string& msg)
	{
		string framed = msg;
		framed += '\0';

		if (opt.replySplitParts <= 1)
		{
			StubSendAll(c, framed.data(), (int)framed.size());
			return;
		}

		int parts = opt.replySplitParts;
		size_t chunk = framed.size() / parts + 1;
		size_t offset = 0;
		for (int i = 0; i < parts && offset < framed.size(); i++)
		{
			size_t n = min(chunk, framed.size() - offset);
			StubSendAll(c, framed.data() + offset, (int)n);
			offset += n;
			if (i < parts - 1)
				Sleep(40);
		}
	}
};

// ---------- UDP-ответчик позывного (для широковещательного режима) ----------

class UdpResponder
{
public:
	int port = 0;

	void Start(const string& callSign)
	{
		s = socket(AF_INET, SOCK_DGRAM, 0);
		SOCKADDR_IN addr;
		memset(&addr, 0, sizeof(addr));
		addr.sin_family = AF_INET;
		addr.sin_addr.s_addr = htonl(INADDR_ANY);
		addr.sin_port = 0;
		if (bind(s, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR)
			return;
		int len = sizeof(addr);
		getsockname(s, (sockaddr*)&addr, &len);
		port = ntohs(addr.sin_port);

		DWORD rcvTimeout = 200;
		setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&rcvTimeout, sizeof(rcvTimeout));
		stopFlag = false;
		worker = thread([this, callSign] { Loop(callSign); });
	}

	~UdpResponder()
	{
		Stop();
	}

	void Stop()
	{
		if (stopFlag.exchange(true))
			return;
		if (s != INVALID_SOCKET)
		{
			closesocket(s);
			s = INVALID_SOCKET;
		}
		if (worker.joinable())
			worker.join();
	}

private:
	SOCKET s = INVALID_SOCKET;
	thread worker;
	atomic<bool> stopFlag{ false };

	void Loop(const string& callSign)
	{
		char buffer[64];
		while (!stopFlag)
		{
			SOCKADDR_IN from;
			int fromLen = sizeof(from);
			int bytes = recvfrom(s, buffer, sizeof(buffer) - 1, 0, (sockaddr*)&from, &fromLen);
			if (bytes <= 0)
				continue;
			buffer[bytes] = '\0';
			if (strcmp(buffer, callSign.c_str()) == 0)
				sendto(s, buffer, bytes, 0, (sockaddr*)&from, fromLen);
		}
	}
};

// ---------- Запуск внешних клиентов ----------

struct ProcResult
{
	int exitCode;
	string output;
};

static string ExeDir()
{
	char path[MAX_PATH];
	GetModuleFileNameA(NULL, path, MAX_PATH);
	string p = path;
	size_t pos = p.find_last_of("\\/");
	return (pos == string::npos) ? "." : p.substr(0, pos);
}

static string FindClientExe(const char* name)
{
	string candidates[2];
	candidates[0] = ExeDir() + "\\" + name + ".exe";
	candidates[1] = string("lab9\\build\\") + name + ".exe";
	for (int i = 0; i < 2; i++)
	{
		FILE* f = fopen(candidates[i].c_str(), "rb");
		if (f)
		{
			fclose(f);
			return candidates[i];
		}
	}
	return "";
}

static ProcResult RunCommand(const string& exe, const string& args)
{
	string cmd = "\"" + exe + "\" " + args + " 2>&1";
	FILE* p = _popen(cmd.c_str(), "rb");
	if (!p)
		return { -1, "popen failed" };
	char buf[4096];
	string out;
	size_t n;
	while ((n = fread(buf, 1, sizeof(buf), p)) > 0)
		out.append(buf, n);
	int rc = _pclose(p);
	return { rc, out };
}

// ---------- Хелперы API-тестов ----------

static ServerAddress MakeLoopbackAddress(int port)
{
	ServerAddress server;
	memset(&server.address, 0, sizeof(server.address));
	server.address.sin_family = AF_INET;
	server.address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	server.address.sin_port = htons((u_short)port);
	server.ip = "127.0.0.1";
	server.port = (short)port;
	return server;
}

static SOCKET ConnectStub(int port, int rcvTimeoutMs)
{
	SOCKET s = ConnectToServer(MakeLoopbackAddress(port));
	if (s != INVALID_SOCKET && rcvTimeoutMs > 0)
	{
		DWORD t = (DWORD)rcvTimeoutMs;
		setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&t, sizeof(t));
	}
	return s;
}

// ---------- API-тесты ----------

// 1. Ответ, разбитый на несколько write, собирается целиком
static void TestEchoSplitReply()
{
	StubOptions opt;
	opt.replySplitParts = 3;
	StubServer stub;
	stub.opt = opt;
	stub.Start();

	SOCKET s = ConnectStub(stub.port, 1000);
	if (s == INVALID_SOCKET)
	{
		Check(false, "echo split: connect stub");
		return;
	}

	bool sentCode = SendRequest(s, "echo");
	const char* msg = "split-reassembly-message-0123456789";
	char answer[256];
	memset(answer, 0, sizeof(answer));
	bool ok = sentCode && RequestEcho(s, msg, answer, sizeof(answer)) && strcmp(answer, msg) == 0;
	Check(ok, "API: текстовый фрейм, разбитый на несколько write, собирается целиком",
		string("answer=\"") + answer + "\"");
	closesocket(s);
	stub.Stop();
}

// 2. Два NUL-фрейма, отправленные одним send, читаются как два отдельных сообщения
static void TestCoalescedFrames()
{
	StubOptions opt;
	opt.coalesceTarget = 2;
	StubServer stub;
	stub.opt = opt;
	stub.Start();

	SOCKET s = ConnectStub(stub.port, 1000);
	if (s == INVALID_SOCKET)
	{
		Check(false, "coalesced: connect stub");
		return;
	}

	SendRequest(s, "echo");
	SendText(s, "coalesced-one");
	SendText(s, "coalesced-two");

	char a1[64], a2[64];
	memset(a1, 0, sizeof(a1));
	memset(a2, 0, sizeof(a2));
	int n1 = ReceiveText(s, a1, sizeof(a1));
	int n2 = ReceiveText(s, a2, sizeof(a2));

	bool ok = n1 > 0 && strcmp(a1, "coalesced-one") == 0
		&& n2 > 0 && strcmp(a2, "coalesced-two") == 0;
	Check(ok, "API: два коалесцированных NUL-фрейма читаются отдельно",
		"n1=" + to_string(n1) + " a1=\"" + string(a1) + "\" n2=" + to_string(n2) + " a2=\"" + string(a2) + "\"");
	closesocket(s);
	stub.Stop();
}

// 3. SendAll: полный ввод доходит при маленьком SO_SNDBUF и мелких чтениях пира
static void TestSendAllSmallBuffer()
{
	StubOptions opt;
	opt.readChunkSize = 256;
	StubServer stub;
	stub.opt = opt;
	stub.Start();

	SOCKET s = ConnectStub(stub.port, 1000);
	if (s == INVALID_SOCKET)
	{
		Check(false, "sendall: connect stub");
		return;
	}

	int sndBuf = 2048;
	setsockopt(s, SOL_SOCKET, SO_SNDBUF, (const char*)&sndBuf, sizeof(sndBuf));

	string big = "BIG:";
	big.append(256 * 1024 - 4, 'x');

	bool sentOk = SendRequest(s, "echo");
	vector<char> answer(300 * 1024, 0);
	bool echoed = sentOk && SendText(s, big.c_str())
		&& ReceiveText(s, answer.data(), (int)answer.size()) > 0;
	bool ok = echoed && strcmp(answer.data(), big.c_str()) == 0;
	Check(ok, "API: SendAll передаёт весь ввод, когда пир читает мелкими кусками",
		ok ? "" : "echo не совпал или отправка неполная");
	closesocket(s);
	stub.Stop();
}

// 4. Частичный rand (2 байта из 4) — отказ, а не «успех»
static void TestRandPartial()
{
	StubOptions opt;
	opt.partialRand = true;
	StubServer stub;
	stub.opt = opt;
	stub.Start();

	SOCKET s = ConnectStub(stub.port, 1000);
	if (s == INVALID_SOCKET)
	{
		Check(false, "rand partial: connect stub");
		return;
	}

	int value = 777;
	bool ok = !RequestRand(s, value) && value == 777;
	Check(ok, "API: частичный 4-байтовый rand возвращает отказ", "value=" + to_string(value));
	closesocket(s);
	stub.Stop();
}

// 5. Полный rand — ровно 4 байта, значение совпадает
static void TestRandOk()
{
	StubServer stub;
	stub.Start();

	SOCKET s = ConnectStub(stub.port, 1000);
	if (s == INVALID_SOCKET)
	{
		Check(false, "rand ok: connect stub");
		return;
	}

	int value = 0;
	bool ok = RequestRand(s, value) && value == 123456;
	Check(ok, "API: rand читает ровно 4 байта", "value=" + to_string(value));
	closesocket(s);
	stub.Stop();
}

// 6. Ответ без NUL (1.5 МБ) — отказ, а не «успех» с обрезанным текстом
static void TestUnframedReply()
{
	StubOptions opt;
	opt.sendUnframedReply = true;
	StubServer stub;
	stub.opt = opt;
	stub.Start();

	SOCKET s = ConnectStub(stub.port, 1000);
	if (s == INVALID_SOCKET)
	{
		Check(false, "unframed: connect stub");
		return;
	}

	SendRequest(s, "echo");
	char answer[128];
	bool ok = !RequestEcho(s, "give-me-the-junk", answer, sizeof(answer));
	Check(ok, "API: не-framed (oversized/без NUL) ответ даёт отказ");
	closesocket(s);
	stub.Stop();
}

// 7. Пир принял запрос, но молчит — отказ по таймауту приёма
static void TestReceiveTimeout()
{
	StubOptions opt;
	opt.silent = true;
	StubServer stub;
	stub.opt = opt;
	stub.Start();

	SOCKET s = ConnectStub(stub.port, 1000);
	if (s == INVALID_SOCKET)
	{
		Check(false, "timeout: connect stub");
		return;
	}

	SendRequest(s, "echo");
	char answer[64];
	DWORD start = GetTickCount();
	bool failed = !RequestEcho(s, "anyone-there", answer, sizeof(answer));
	DWORD elapsed = GetTickCount() - start;
	// SO_RCVTIMEO в тесте 1000 мс + запас на планировщик
	Check(failed && elapsed < 4000, "API: молчащий пир — отказ по таймауту приёма",
		"elapsed=" + to_string(elapsed) + "мс");
	closesocket(s);
	stub.Stop();
}

// ---------- DNS-порт выше signed short (Windows TCP-порты 32768..65535) ----------
static void TestDnsHighPort()
{
	SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (listener == INVALID_SOCKET)
	{
		Check(false, "DNS: высокий TCP-порт — socket");
		return;
	}

	SOCKADDR_IN address;
	memset(&address, 0, sizeof(address));
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	bool bound = false;
	int chosenPort = 0;

	for (int port = 45000; port <= 60000; port++)
	{
		address.sin_port = htons((u_short)port);
		if (bind(listener, (sockaddr*)&address, sizeof(address)) == 0)
		{
			chosenPort = port;
			bound = true;
			break;
		}
	}

	if (!bound || listen(listener, 1) == SOCKET_ERROR)
	{
		closesocket(listener);
		Check(false, "DNS: высокий TCP-порт — удалось поднять listener >= 32768");
		return;
	}

	ServerAddress server;
	bool ok = FindServerByName("localhost", (short)chosenPort, server, 2000);
	Check(ok && (unsigned short)server.port == (unsigned short)chosenPort,
		"DNS: TCP-порт выше 32767 сохраняется без знакового переполнения",
		"port=" + to_string(chosenPort) + " lastError=" + to_string(WSAGetLastError()));
	closesocket(listener);
}

// ---------- CLI-тесты (реальные exe клиентов) ----------

static void CliDnsEchoOk()
{
	string exe = FindClientExe("ClientEcho");
	if (exe.empty()) { Check(false, "CLI: --dns echo — ClientEcho.exe не найден"); return; }

	StubServer stub;
	stub.Start();
	ProcResult r = RunCommand(exe, "--dns localhost " + to_string(stub.port) + " 2 10");
	bool ok = r.exitCode == 0 && r.output.find("корректных ответов 2 из 2") != string::npos;
	Check(ok, "CLI: ClientEcho --dns localhost — успех, код 0",
		"rc=" + to_string(r.exitCode) + " out=" + r.output.substr(0, 200));
	stub.Stop();
}

static void CliDnsTimeOk()
{
	string exe = FindClientExe("ClientTime");
	if (exe.empty()) { Check(false, "CLI: --dns time — ClientTime.exe не найден"); return; }

	StubServer stub;
	stub.Start();
	ProcResult r = RunCommand(exe, "--dns localhost " + to_string(stub.port) + " 2");
	bool ok = r.exitCode == 0 && r.output.find("корректных ответов 2 из 2") != string::npos;
	Check(ok, "CLI: ClientTime --dns localhost — успех, код 0",
		"rc=" + to_string(r.exitCode) + " out=" + r.output.substr(0, 200));
	stub.Stop();
}

static void CliDnsRandOk()
{
	string exe = FindClientExe("ClientRand");
	if (exe.empty()) { Check(false, "CLI: --dns rand — ClientRand.exe не найден"); return; }

	StubServer stub;
	stub.Start();
	ProcResult r = RunCommand(exe, "--dns localhost " + to_string(stub.port) + " 2");
	bool ok = r.exitCode == 0 && r.output.find("корректных ответов 2 из 2") != string::npos;
	Check(ok, "CLI: ClientRand --dns localhost — успех, код 0",
		"rc=" + to_string(r.exitCode) + " out=" + r.output.substr(0, 200));
	stub.Stop();
}

static void CliDnsBadHost()
{
	string exe = FindClientExe("ClientEcho");
	if (exe.empty()) { Check(false, "CLI: --dns badhost — ClientEcho.exe не найден"); return; }

	StubServer stub;
	stub.Start();
	ProcResult r = RunCommand(exe, "--dns no-such-host-abc123.invalid " + to_string(stub.port) + " 1 10");
	bool ok = r.exitCode != 0 && r.output.find("разрешить") != string::npos;
	Check(ok, "CLI: --dns с неслурезаемым именем — ненулевой код и сообщение про DNS",
		"rc=" + to_string(r.exitCode) + " out=" + r.output.substr(0, 200));
	stub.Stop();
}

static void CliBadPortSyntax()
{
	string exe = FindClientExe("ClientEcho");
	if (exe.empty()) { Check(false, "CLI: bad port — ClientEcho.exe не найден"); return; }

	ProcResult r = RunCommand(exe, "--dns localhost notaport 1 10");
	bool ok = r.exitCode != 0 && r.output.find("некорректн") != string::npos;
	Check(ok, "CLI: некорректный синтаксис TCP-порта — ненулевой код",
		"rc=" + to_string(r.exitCode) + " out=" + r.output.substr(0, 200));
}

static void CliBadCountSyntax()
{
	string exe = FindClientExe("ClientEcho");
	if (exe.empty()) { Check(false, "CLI: bad count — ClientEcho.exe не найден"); return; }

	ProcResult r = RunCommand(exe, "--dns localhost 1 five");
	bool ok = r.exitCode != 0 && r.output.find("некорректн") != string::npos;
	Check(ok, "CLI: некорректный синтаксис количества — ненулевой код",
		"rc=" + to_string(r.exitCode) + " out=" + r.output.substr(0, 200));
}

static void CliNegativeCount()
{
	string exe = FindClientExe("ClientEcho");
	if (exe.empty()) { Check(false, "CLI: negative count — ClientEcho.exe не найден"); return; }

	ProcResult r = RunCommand(exe, "--dns localhost 1 -5 10");
	bool ok = r.exitCode != 0 && r.output.find("некорректн") != string::npos;
	Check(ok, "CLI: отрицательное количество — ненулевой код",
		"rc=" + to_string(r.exitCode) + " out=" + r.output.substr(0, 200));
}

static void CliBroadcastRegression()
{
	string exe = FindClientExe("ClientEcho");
	if (exe.empty()) { Check(false, "CLI: broadcast — ClientEcho.exe не найден"); return; }

	StubServer stub;
	stub.Start();
	UdpResponder udp;
	udp.Start("TestServer");

	ProcResult r = RunCommand(exe, "TestServer " + to_string(udp.port) + " " + to_string(stub.port) + " 2 10");
	bool ok = r.exitCode == 0 && r.output.find("корректных ответов 2 из 2") != string::npos;
	Check(ok, "CLI: прежний широковещательный запуск продолжает работать",
		"rc=" + to_string(r.exitCode) + " out=" + r.output.substr(0, 200));
	stub.Stop();
	udp.Stop();
}

static void CliServerDropsMidway()
{
	string exe = FindClientExe("ClientEcho");
	if (exe.empty()) { Check(false, "CLI: drop midway — ClientEcho.exe не найден"); return; }

	StubOptions opt;
	opt.closeAfterFirstReply = true;
	StubServer stub;
	stub.opt = opt;
	stub.Start();

	ProcResult r = RunCommand(exe, "--dns localhost " + to_string(stub.port) + " 3 10");
	bool ok = r.exitCode != 0 && r.output.find("корректных ответов 1 из 3") != string::npos;
	Check(ok, "CLI: обрыв сервера в середине серии — ненулевой код и точный счёт",
		"rc=" + to_string(r.exitCode) + " out=" + r.output.substr(0, 300));
	stub.Stop();
}

int main()
{
	WSADATA wsaData;
	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
	{
		printf("WSAStartup failed\n");
		return 1;
	}

	printf("=== ЛР9 ClientApi regression tests ===\n");

	TestEchoSplitReply();
	TestCoalescedFrames();
	TestSendAllSmallBuffer();
	TestRandPartial();
	TestRandOk();
	TestUnframedReply();
	TestReceiveTimeout();
	TestDnsHighPort();

	CliDnsEchoOk();
	CliDnsTimeOk();
	CliDnsRandOk();
	CliDnsBadHost();
	CliBadPortSyntax();
	CliBadCountSyntax();
	CliNegativeCount();
	CliBroadcastRegression();
	CliServerDropsMidway();

	printf("=== Итого: PASS=%d FAIL=%d ===\n", g_pass, g_fail);

	WSACleanup();
	return g_fail == 0 ? 0 : 1;
}
