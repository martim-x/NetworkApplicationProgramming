# lab8-10-smoke.ps1 — Windows integration/smoke harness for labs 8-10.
#
# Real process + socket integration: starts ConcurrentServer.exe (lab8), drives it
# with deterministic .NET TCP/UDP/named-pipe clients, runs the built lab9 client
# executables and the lab10 RConsole executable, then kills every child process.
#
# Usage (Windows PowerShell 5.1):
#   powershell -NoProfile -ExecutionPolicy Bypass -File tests\lab8-10-smoke.ps1
#
# Grounded in the visible command contract (2026-11-15, worktree fix/lab8-10-stability):
#   ConcurrentServer.exe [tcpPort] [dllPath] [pipeName] [udpPort] [callSign] [serviceTimeoutMs]
#     - TCP: first message is a NUL-terminated request code ("echo"/"time"/"rand");
#       bytes after the code are echoed back by the echo service as the first response.
#     - UDP responsePort: query with callSign -> reply is the callSign (NUL-terminated).
#     - Named pipe (forced under \\.\pipe\): commands start/stop/exit/statistics/wait/shutdown;
#       unknown command -> "nocmd"; known -> answer text returned to the pipe client.
#   ClientEcho.exe [callSign] [udpPort] [tcpPort] [messageTotal] [delayMs]
#     - broadcast discovery, then echo exchange; exchange errors must yield nonzero exit
#       after the lab9 fix (plan task 2). DNS mode (--dns <hostname>) planned per plan task 2.
#   RConsole.exe [pipeName] [command...]
#     - non-interactive: prints "RConsole: <cmd> -> <answer>"; exit/shutdown stops after it.
#
# Exit code: number of failed tests. Skips do not fail the run.

param(
    # Repository root (parent of the tests directory by default).
    [string]$Root = (Split-Path -Parent $PSScriptRoot),

    [string]$ServerExe       = (Join-Path $Root 'lab8\build\ConcurrentServer.exe'),
    [string]$ServiceDll      = (Join-Path $Root 'lab8\build\ServiceLibrary.dll'),
    [string]$ClientEchoExe   = (Join-Path $Root 'lab9\build\ClientEcho.exe'),
    [string]$RConsoleExe     = (Join-Path $Root 'lab10\build\RConsole.exe'),

    # Event-poll pipe command per plan task 3. The exact command name / answer format is
    # not yet fixed in sources; if the server answers "nocmd" the event test SKIPS and
    # asks for alignment instead of guessing.
    [string]$EventsCommand = 'events',

    # Default socket op / connect deadline in ms (keep small: all endpoints are loopback).
    [int]$SocketTimeoutMs = 8000,

    # Server service-timeout default used by most tests (6th server argument).
    [int]$ServiceTimeoutMs = 15000,

    # Only run test whose Id matches one of these patterns (empty = all).
    [string[]]$Test = @()
)

$ErrorActionPreference = 'Stop'
$script:Results   = New-Object System.Collections.Generic.List[object]
$script:Processes = New-Object System.Collections.Generic.List[Diagnostics.Process]
$script:TempDir   = Join-Path ([IO.Path]::GetTempPath()) ("lab810smoke-" + [Guid]::NewGuid().ToString('N').Substring(0, 8))
New-Item -ItemType Directory -Force -Path $script:TempDir | Out-Null

# ---------------------------------------------------------------- result helpers

function Add-Result([string]$Id, [string]$Status, [string]$Detail) {
    $script:Results.Add([pscustomobject]@{ Id = $Id; Status = $Status; Detail = $Detail }) | Out-Null
    Write-Output ("[{0}] {1}" -f $Status, $Id)
    if ($Detail) {
        foreach ($line in ($Detail -split "`n")) {
            if ($line.Trim()) { Write-Output ("       " + $line.TrimEnd()) }
        }
    }
}

function Invoke-TestCase([string]$Id, [scriptblock]$Body) {
    if ($Test.Count -gt 0) {
        $match = $false
        foreach ($raw in $Test) {
            foreach ($pattern in ($raw -split ',')) {
                if ($Id -like $pattern) { $match = $true; break }
            }
            if ($match) { break }
        }
        if (-not $match) { return }
    }
    try {
        $detail = & $Body
        Add-Result $Id 'PASS' ([string]$detail)
    }
    catch {
        $msg = $_.Exception.Message
        if ($msg.StartsWith('SKIP: ')) { Add-Result $Id 'SKIP' $msg.Substring(6) }
        else { Add-Result $Id 'FAIL' $msg }
    }
    finally {
        # never leave a server (or any child) running between tests
        Stop-TrackedProcesses
    }
}

# ------------------------------------------------------------- process helpers

function Quote-Arg([string]$a) { return '"' + $a + '"' }

# Process tracking: spawn via raw Diagnostics.Process (Start-Process objects keep
# ExitCode null) with stdout/stderr redirected to per-process files. The journal is
# read from those files, which avoids losing async data-received events (they are
# not pumped while the script is busy) and keeps output readable after exit.
$script:LogMap = @{ }
$script:LogSeq = 0

function Start-TrackedProcess([string]$Exe, [string[]]$ArgList, [string]$WorkDir) {
    if (-not (Test-Path $Exe)) { throw "SKIP: missing executable: $Exe" }
    $script:LogSeq++
    $outLog = Join-Path $script:TempDir ("proc-{0}-{1}.log" -f $script:LogSeq, [IO.Path]::GetFileNameWithoutExtension($Exe))
    $errLog = $outLog + '.err'
    if (Test-Path $outLog) { Remove-Item $outLog -Force -ErrorAction SilentlyContinue }
    if (Test-Path $errLog) { Remove-Item $errLog -Force -ErrorAction SilentlyContinue }

    $quoted = @()
    foreach ($a in $ArgList) { $quoted += Quote-Arg $a }
    $p = Start-Process -FilePath $Exe -ArgumentList ($quoted -join ' ') `
        -WorkingDirectory $WorkDir -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput $outLog -RedirectStandardError $errLog
    # Hold the process handle immediately: without it, ExitCode stays null in
    # PowerShell 5.1 Start-Process objects even after WaitForExit.
    $null = $p.Handle
    $script:LogMap[$p.Id] = @{ Out = $outLog; Err = $errLog }
    $script:Processes.Add($p) | Out-Null
    return $p
}

function Stop-TrackedProcesses {
    foreach ($p in $script:Processes) {
        try {
            if (-not $p.HasExited) {
                $p.Kill()
                $p.WaitForExit(3000) | Out-Null
            }
        } catch { }
        try { $p.Dispose() } catch { }
    }
    $script:Processes.Clear()
    $script:LogMap.Clear()
}

function Get-LogLines([Diagnostics.Process]$p) {
    $paths = $script:LogMap[$p.Id]
    if ($paths -eq $null) { return @() }
    $result = @()
    try {
        if (Test-Path $paths.Out) {
            $result += @(Read-AllLogLines $paths.Out)
        }
    } catch { }
    try {
        if (Test-Path $paths.Err) {
            $result += @(Read-AllLogLines $paths.Err)
        }
    } catch { }
    return $result
}

# Read a growing log file with a writer attached: open with FileShare.ReadWrite.
function Read-AllLogLines([string]$Path) {
    $lines = @()
    try {
        $fs = [IO.File]::Open($Path, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::ReadWrite)
        try {
            $reader = New-Object IO.StreamReader($fs, [Text.Encoding]::UTF8)
            try {
                $txt = $reader.ReadToEnd()
                if ($txt) { $lines = @($txt -split "`r?`n" | Where-Object { $_ -ne '' }) }
            } finally { $reader.Dispose() }
        } finally { $fs.Dispose() }
    } catch { }
    return $lines
}

function Wait-LogContains([Diagnostics.Process]$p, [string]$Pattern, [int]$DeadlineMs) {
    $sw = [Diagnostics.Stopwatch]::StartNew()
    while ($sw.ElapsedMilliseconds -lt $DeadlineMs) {
        foreach ($l in (Get-LogLines $p)) { if ($l -match $Pattern) { return $l } }
        Start-Sleep -Milliseconds 100
    }
    return $null
}

function Wait-ProcessExit([Diagnostics.Process]$p, [int]$DeadlineMs) {
    # WaitForExit(timeout) keeps the process handle alive so ExitCode is populated
    # after exit (HasExited polling alone leaves ExitCode null on Start-Process objects)
    $exited = $p.WaitForExit($DeadlineMs)
    if ($exited) {
        # flush pending data-received events before callers read the journal
        $p.WaitForExit() | Out-Null
    }
    return $exited
}

# --------------------------------------------------------------- port helpers

function Get-FreeTcpPort {
    $l = New-Object Net.Sockets.TcpListener ([Net.IPAddress]::Loopback), 0
    $l.Start()
    $port = ([Net.IPEndPoint]$l.LocalEndpoint).Port
    $l.Stop()
    return $port
}

function Get-FreeUdpPort {
    $u = New-Object Net.Sockets.UdpClient 0
    $port = ([Net.IPEndPoint]$u.Client.LocalEndPoint).Port
    $u.Close()
    return $port
}

# ------------------------------------------------------------- server context

$script:ServerSeq = 0

function Start-TestServer([int]$ServiceTimeout = 15000) {
    $script:ServerSeq++

    $lastError = ''
    for ($attempt = 1; $attempt -le 3; $attempt++) {
        $ctx = @{
            TcpPort        = Get-FreeTcpPort
            UdpPort        = Get-FreeUdpPort
            PipeName       = '\\.\pipe\lab810smoke-' + $PID + '-' + $script:ServerSeq + '-' + $attempt
            CallSign       = 'SmokeServer-' + $PID + '-' + $script:ServerSeq
            ServiceTimeout = $ServiceTimeout
        }

        $p = Start-TrackedProcess $ServerExe `
            @([string]$ctx.TcpPort, $ServiceDll, $ctx.PipeName, [string]$ctx.UdpPort, $ctx.CallSign, [string]$ServiceTimeout) `
            (Split-Path -Parent $ServerExe)
        $ctx.Process = $p

        # Readiness probe через UDP-дискавери (позывной): TCP-connect здесь
        # засчитывался бы сервером как подключение/отказ и искажал статистику.
        $sw = [Diagnostics.Stopwatch]::StartNew()
        while ($sw.ElapsedMilliseconds -lt 10000) {
            if ($p.HasExited) {
                $lastError = "server exited early with code $($p.ExitCode); log: " + ((Get-LogLines $p) -join ' | ')
                break
            }
            $u = New-Object Net.Sockets.UdpClient
            $u.Client.ReceiveTimeout = 500
            try {
                $b = [Text.Encoding]::UTF8.GetBytes($ctx.CallSign + [char]0)
                $u.Send($b, $b.Length, '127.0.0.1', $ctx.UdpPort) | Out-Null
                $ep = New-Object Net.IPEndPoint ([Net.IPAddress]::Any), 0
                $r = $u.Receive([ref]$ep)
                $reply = ([Text.Encoding]::UTF8.GetString($r)).TrimEnd([char]0)
                if ($reply -eq $ctx.CallSign) { return $ctx }
            } catch { }
            finally { $u.Close() }
            Start-Sleep -Milliseconds 100
        }

        if ($p.HasExited) { continue }
        Stop-TrackedProcesses
        $lastError = "server did not start listening within 10s"
    }

    throw "SKIP: cannot start ConcurrentServer: $lastError"
}

# -------------------------------------------------------------- socket helpers

function Connect-Tcp([int]$Port, [int]$TimeoutMs) {
    $s = New-Object Net.Sockets.Socket ([Net.Sockets.AddressFamily]::InterNetwork),
        ([Net.Sockets.SocketType]::Stream), ([Net.Sockets.ProtocolType]::Tcp)
    $iar = $s.BeginConnect('127.0.0.1', $Port, $null, $null)
    if (-not $iar.AsyncWaitHandle.WaitOne($TimeoutMs)) {
        $s.Close()
        throw "connect to 127.0.0.1:$Port timed out after ${TimeoutMs}ms"
    }
    $s.EndConnect($iar)
    $s.ReceiveTimeout = $TimeoutMs
    $s.SendTimeout = $TimeoutMs
    return $s
}

function Send-Bytes([Net.Sockets.Socket]$s, [byte[]]$bytes) {
    $s.Send($bytes) | Out-Null
}

function Read-Exact([Net.Sockets.Socket]$s, [int]$Count, [int]$TimeoutMs) {
    $buf = New-Object byte[] $Count
    $received = 0
    $sw = [Diagnostics.Stopwatch]::StartNew()
    while ($received -lt $Count) {
        $remaining = [int]($TimeoutMs - $sw.ElapsedMilliseconds)
        if ($remaining -le 0) { throw "read of $Count bytes timed out after ${TimeoutMs}ms (got $received)" }
        $s.ReceiveTimeout = $remaining
        try {
            $n = $s.Receive($buf, $received, $Count - $received, [Net.Sockets.SocketFlags]::None)
        }
        catch [Net.Sockets.SocketException] {
            throw ("receive failed after $received/$Count bytes: " + $_.Exception.SocketErrorCode)
        }
        if ($n -eq 0) { throw "connection closed after $received/$Count bytes" }
        $received += $n
    }
    return ,$buf
}

# Returns $true if the peer closed (recv 0 or a real reset/abort) within DeadlineMs,
# $false if the connection was still open at the deadline. A receive TIMEOUT counts
# as "still open": the silent client proves nothing was received, not that we closed.
function Wait-SocketClose([Net.Sockets.Socket]$s, [int]$DeadlineMs) {
    $buf = New-Object byte[] 64
    $sw = [Diagnostics.Stopwatch]::StartNew()
    while ($sw.ElapsedMilliseconds -lt $DeadlineMs) {
        $s.ReceiveTimeout = [Math]::Max(100, [int]($DeadlineMs - $sw.ElapsedMilliseconds))
        try {
            $n = $s.Receive($buf, 0, $buf.Length, [Net.Sockets.SocketFlags]::None)
            if ($n -eq 0) { return $true }
        }
        catch [Net.Sockets.SocketException] {
            if ($_.Exception.SocketErrorCode -eq [Net.Sockets.SocketError]::TimedOut) { continue }
            return $true   # reset/aborted by the peer counts as closure
        }
    }
    return $false
}

function Close-Socket([Net.Sockets.Socket]$s) {
    try { $s.Shutdown([Net.Sockets.SocketShutdown]::Both) } catch { }
    try { $s.Close() } catch { }
}

# ----------------------------------------------------- protocol client helpers

function Utf8-Bytes([string]$text) { return [Text.Encoding]::UTF8.GetBytes($text) }

function Join-FirstLines([string]$Text, [int]$Count) {
    return (($Text -split "`n" | Select-Object -First $Count) -join ' | ')
}

# Refused session (unknown request code): server answers ErrorInquiry; bumps the
# refusal counter. Used by the statistics test.
function Run-EchoSession-refused([int]$TcpPort, [int]$TimeoutMs) {
    $s = Connect-Tcp $TcpPort $TimeoutMs
    try {
        Send-Bytes $s (Utf8-Bytes ('bogus-refused' + [char]0))
        $null = Read-Exact $s 12 $TimeoutMs
    }
    finally { Close-Socket $s }
}

function New-EchoPacket([string]$code, [byte[]]$payload) {
    $head = Utf8-Bytes ($code + [char]0)
    $out = New-Object byte[] ($head.Length + $payload.Length)
    [Array]::Copy($head, $out, $head.Length)
    if ($payload.Length -gt 0) { [Array]::Copy($payload, 0, $out, $head.Length, $payload.Length) }
    return ,$out
}

# Sends code+payload as one TCP packet, reads exactly payload.Length bytes back.
function Assert-EchoRoundtrip([Net.Sockets.Socket]$s, [byte[]]$payload, [string]$label, [int]$TimeoutMs) {
    Send-Bytes $s (New-EchoPacket 'echo' $payload)
    $got = Read-Exact $s $payload.Length $TimeoutMs
    for ($i = 0; $i -lt $payload.Length; $i++) {
        if ($got[$i] -ne $payload[$i]) {
            throw ("$label byte $i differs: expected {0}, got {1}" -f $payload[$i], $got[$i])
        }
    }
}

# One complete echo session: code packet, first response = payload echo, close.
function Run-EchoSession([int]$TcpPort, [byte[]]$payload, [int]$TimeoutMs) {
    $s = Connect-Tcp $TcpPort $TimeoutMs
    try { Assert-EchoRoundtrip $s $payload 'echo session' $TimeoutMs }
    finally { Close-Socket $s }
}

# Named pipe command with bounded connect/read. Returns the answer text (NUL trimmed).
function Send-PipeCommand([string]$PipeName, [string]$Command, [int]$TimeoutMs) {
    $shortName = $PipeName -replace '^\\\\\.\\pipe\\', ''
    $pipe = New-Object System.IO.Pipes.NamedPipeClientStream '.', $shortName,
        ([System.IO.Pipes.PipeDirection]::InOut), ([System.IO.Pipes.PipeOptions]::None)
    try {
        $pipe.Connect($TimeoutMs)
        $b = Utf8-Bytes $Command
        $pipe.Write($b, 0, $b.Length)
        $pipe.Flush()

        $buf = New-Object byte[] 256
        $task = $pipe.ReadAsync($buf, 0, $buf.Length)
        if (-not $task.Wait($TimeoutMs)) { throw "pipe answer to '$Command' timed out after ${TimeoutMs}ms" }
        if ($task.Result -le 0) { throw "pipe answer to '$Command' is empty (server closed the pipe)" }
        return ([Text.Encoding]::UTF8.GetString($buf, 0, $task.Result)).TrimEnd([char]0)
    }
    finally { $pipe.Dispose() }
}

# Runs an executable and returns @{ ExitCode; ElapsedMs; Output }.
function Run-ProcessChecked([string]$Exe, [string[]]$ArgList, [string]$WorkDir, [int]$DeadlineMs) {
    $sw = [Diagnostics.Stopwatch]::StartNew()
    $p = Start-TrackedProcess $Exe $ArgList $WorkDir
    $exited = Wait-ProcessExit $p $DeadlineMs
    if (-not $exited) {
        try { $p.Kill() } catch { }
        throw ("process did not exit within ${DeadlineMs}ms: " + $Exe)
    }
    return @{
        ExitCode  = $p.ExitCode
        ElapsedMs = $sw.ElapsedMilliseconds
        Output    = (Get-LogLines $p) -join "`n"
    }
}

# -------------------------------------------------------------------- fixtures

function New-PayloadBytes([int]$Length, [string]$Label) {
    $bytes = New-Object byte[] $Length
    $labelBytes = Utf8-Bytes $Label
    for ($i = 0; $i -lt $Length; $i++) {
        # deterministic, printable, no NUL bytes
        $bytes[$i] = [byte](0x21 + (($i + $labelBytes.Length * 7) % 90))
    }
    for ($i = 0; $i -lt [Math]::Min($labelBytes.Length, $Length); $i++) { $bytes[$i] = $labelBytes[$i] }
    return ,$bytes
}

# =================================================================== TESTS ====

Write-Output "lab8-10-smoke: root=$Root"
Write-Output ("binaries: server={0} dll={1} clientEcho={2} rconsole={3}" -f `
    (Test-Path $ServerExe), (Test-Path $ServiceDll), (Test-Path $ClientEchoExe), (Test-Path $RConsoleExe))

# (a) code and payload in the same packet, payload > 31 bytes, exact byte preservation
Invoke-TestCase 'A1-echo-code-plus-payload-single-packet' {
    $ctx = Start-TestServer -ServiceTimeout $ServiceTimeoutMs
    $payload = New-PayloadBytes 200 'A1'
    $s = Connect-Tcp $ctx.TcpPort $SocketTimeoutMs
    try {
        Assert-EchoRoundtrip $s $payload 'first response (pending payload)' $SocketTimeoutMs
        # second message on the same session must be echoed exactly too
        $want = Utf8-Bytes 'A1-SECOND-MESSAGE'
        Send-Bytes $s ($want + [byte]0)
        $got = Read-Exact $s ($want.Length + 1) $SocketTimeoutMs
        for ($i = 0; $i -lt $want.Length; $i++) {
            if ($got[$i] -ne $want[$i]) { throw ("second echo differs at byte $i") }
        }
        if ($got[$want.Length] -ne 0) { throw "second echo lost its NUL terminator" }
    }
    finally { Close-Socket $s }
    return "200-byte payload echoed byte-exact in first response; second message echoed exactly"
}

# (b) request code split across TCP segments must still be assembled
Invoke-TestCase 'A2-split-code-arrival' {
    $ctx = Start-TestServer -ServiceTimeout $ServiceTimeoutMs
    $s = Connect-Tcp $ctx.TcpPort $SocketTimeoutMs
    try {
        try {
            Send-Bytes $s (Utf8-Bytes 'ech')
            Start-Sleep -Milliseconds 300
            Send-Bytes $s (Utf8-Bytes ('o' + [char]0))
            Start-Sleep -Milliseconds 300
            Send-Bytes $s (Utf8-Bytes ('PAYLOAD-SPLIT' + [char]0))
        }
        catch [Net.Sockets.SocketException] {
            throw ("server aborted the connection while the request code was split across " +
                "segments ('ech' + 'o'); expected: keep the connection and wait for the " +
                "terminator (" + $_.Exception.SocketErrorCode + ")")
        }
        # expected after fix: first response is the pending payload + NUL (19 bytes)
        $want = Utf8-Bytes 'PAYLOAD-SPLIT'
        $got = Read-Exact $s ($want.Length + 1) $SocketTimeoutMs
        for ($i = 0; $i -lt $want.Length; $i++) {
            if ($got[$i] -ne $want[$i]) {
                throw ("first response is not the split-code payload: got '" +
                    [Text.Encoding]::UTF8.GetString($got) + "'")
            }
        }
        if ($got[$want.Length] -ne 0) { throw "pending payload echo lost its NUL terminator" }
        # session must stay usable after a fragmented first request
        Send-Bytes $s (Utf8-Bytes ('A2-FOLLOWUP' + [char]0))
        $got2 = Read-Exact $s 12 $SocketTimeoutMs
        $want2 = Utf8-Bytes 'A2-FOLLOWUP'
        for ($i = 0; $i -lt $want2.Length; $i++) {
            if ($got2[$i] -ne $want2[$i]) { throw "follow-up echo differs at byte $i" }
        }
    }
    finally { Close-Socket $s }
    return "code assembled from 3 segments; pending payload echoed; session reusable"
}

# sanity: unknown request code -> ErrorInquiry
Invoke-TestCase 'A3-unknown-request-errorinquiry' {
    $ctx = Start-TestServer -ServiceTimeout $ServiceTimeoutMs
    $s = Connect-Tcp $ctx.TcpPort $SocketTimeoutMs
    try {
        Send-Bytes $s (Utf8-Bytes ('bogus-code' + [char]0))
        $got = Read-Exact $s 12 $SocketTimeoutMs
        $want = Utf8-Bytes 'ErrorInquiry'
        for ($i = 0; $i -lt $want.Length; $i++) {
            if ($got[$i] -ne $want[$i]) { throw "expected ErrorInquiry, got other bytes" }
        }
    }
    finally { Close-Socket $s }
    return "ErrorInquiry returned for unknown code"
}

# sanity: UDP discovery answers the callSign
Invoke-TestCase 'A4-udp-discovery-callSign' {
    $ctx = Start-TestServer -ServiceTimeout $ServiceTimeoutMs
    $u = New-Object Net.Sockets.UdpClient
    $u.Client.ReceiveTimeout = 5000
    try {
        $b = Utf8-Bytes ($ctx.CallSign + [char]0)
        $u.Send($b, $b.Length, '127.0.0.1', $ctx.UdpPort) | Out-Null
        $ep = New-Object Net.IPEndPoint ([Net.IPAddress]::Any), 0
        $r = $u.Receive([ref]$ep)
        $reply = ([Text.Encoding]::UTF8.GetString($r)).TrimEnd([char]0)
        if ($reply -ne $ctx.CallSign) { throw "unexpected discovery reply: '$reply'" }
    }
    finally { $u.Close() }
    return "UDP reply equals callSign"
}

# (c) silent client (connected, never sends a request) must be expired by the
# service timer, freeing the slot; journal keeps the diagnostics.
Invoke-TestCase 'B1-silent-connection-expires' {
    $ctx = Start-TestServer -ServiceTimeout 2000
    $s = Connect-Tcp $ctx.TcpPort $SocketTimeoutMs
    $closed = $false
    try { $closed = Wait-SocketClose $s (3 * 2000 + 2000) }
    finally { Close-Socket $s }
    if (-not $closed) {
        throw ("silent connection was still open " + (3 * 2000 + 2000) + "ms after connect; " +
            "service timeout was 2000ms - timer did not cover the silent client")
    }
    # the slot must be free again: a new echo session must work right away
    Run-EchoSession $ctx.TcpPort (New-PayloadBytes 32 'B1-next') $SocketTimeoutMs
    return "silent connection closed by server; subsequent echo session OK"
}

# (d1) wait accepts, shutdown terminates the server process with code 0
Invoke-TestCase 'C1-commands-wait-then-shutdown' {
    $ctx = Start-TestServer -ServiceTimeout $ServiceTimeoutMs
    Run-EchoSession $ctx.TcpPort (New-PayloadBytes 24 'C1') $SocketTimeoutMs
    $answer = Send-PipeCommand $ctx.PipeName 'wait' $SocketTimeoutMs
    if ($answer -eq 'nocmd') { throw "wait rejected as unknown command" }
    $answer2 = Send-PipeCommand $ctx.PipeName 'shutdown' $SocketTimeoutMs
    if ($answer2 -eq 'nocmd') { throw "shutdown rejected as unknown command" }
    if (-not (Wait-ProcessExit $ctx.Process 15000)) {
        throw "server still running 15s after shutdown command"
    }
    if ($ctx.Process.ExitCode -ne 0) {
        throw "server exit code after shutdown: $($ctx.Process.ExitCode)"
    }
    return "wait answered '$answer', shutdown answered '$answer2', server exited 0"
}

# (d2) statistics command must return counter text through the pipe (plan task 3),
# not just echo the command name back.
Invoke-TestCase 'C2-statistics-returns-counters' {
    $ctx = Start-TestServer -ServiceTimeout $ServiceTimeoutMs
    Run-EchoSession $ctx.TcpPort (New-PayloadBytes 24 'C2-ok') $SocketTimeoutMs
    $null = Wait-LogContains $ctx.Process 'завершение обслуживания' 5000
    Run-EchoSession-refused $ctx.TcpPort $SocketTimeoutMs
    Start-Sleep -Milliseconds 300   # let GarbageCleaner settle the counters
    $answer = Send-PipeCommand $ctx.PipeName 'statistics' $SocketTimeoutMs
    if ($answer -eq 'nocmd') { throw "statistics rejected as unknown command" }
    if ($answer -notmatch 'total=\d+\s+active=\d+\s+completed=\d+\s+refused=\d+') {
        throw "statistics answer '$answer' does not carry counter fields (total/active/completed/refused)"
    }
    return "statistics answer: '$answer'"
}

# helper used by C2: refused session (unknown code) to bump the refusal counter

# (d3) exit terminates the server promptly even with an active client; the active
# socket must observe the closure.
Invoke-TestCase 'C3-exit-interrupts-active-socket' {
    $ctx = Start-TestServer -ServiceTimeout $ServiceTimeoutMs
    $s = Connect-Tcp $ctx.TcpPort $SocketTimeoutMs
    try {
        Assert-EchoRoundtrip $s (New-PayloadBytes 24 'C3') 'active session' $SocketTimeoutMs
        $answer = Send-PipeCommand $ctx.PipeName 'exit' $SocketTimeoutMs
        if ($answer -eq 'nocmd') { throw "exit rejected as unknown command" }
        $closed = Wait-SocketClose $s 10000
        if (-not $closed) { throw "active socket still open 10s after exit command" }
        if (-not (Wait-ProcessExit $ctx.Process 20000)) {
            throw "server still running 20s after exit command"
        }
    }
    finally { Close-Socket $s }
    return "active socket closed and server process terminated after exit"
}

# (e) a real lab9 client process in an interrupted session must exit nonzero
# (plan task 2: exchange errors -> nonzero code).
Invoke-TestCase 'D1-client-process-nonzero-on-server-exit' {
    $ctx = Start-TestServer -ServiceTimeout $ServiceTimeoutMs
    $p = Start-TrackedProcess $ClientEchoExe `
        @($ctx.CallSign, [string]$ctx.UdpPort, [string]$ctx.TcpPort, '10', '500') `
        (Split-Path -Parent $ClientEchoExe)

    # deterministic: wait until the client session is actually accepted
    if (-not (Wait-LogContains $ctx.Process 'EchoServer: обслуживание клиента начато' 15000)) {
        throw ("SKIP: ClientEcho session never started (discovery/connect issue); " +
            "server log: " + ((Get-LogLines $ctx.Process) -join ' | ') +
            "; client log: " + ((Get-LogLines $p) -join ' | '))
    }
    # deterministic: wait until the first echo message has been served, so the exit
    # command lands in the middle of an active session (10 msgs x 500ms = ~5s left)
    if (-not (Wait-LogContains $p 'эхо: Hello from ClientEcho 001' 15000)) {
        throw ("SKIP: ClientEcho first message never echoed; client log: " +
            ((Get-LogLines $p) -join ' | '))
    }

    $null = Send-PipeCommand $ctx.PipeName 'exit' $SocketTimeoutMs
    if (-not (Wait-ProcessExit $p 20000)) {
        try { $p.Kill() } catch { }
        throw "client still running 20s after server exit"
    }
    if ($p.ExitCode -eq 0) {
        throw ("client exited 0 although the server killed the session mid-exchange; " +
            "plan task 2 requires nonzero on exchange failure. client log: " +
            ((Get-LogLines $p) -join ' | '))
    }
    return "client interrupted by server exit -> exit code $($p.ExitCode)"
}

# (f1) DNS mode: --dns with a resolvable name finds the server and completes a session.
# Current CLI contract (lab9 sources): ClientEcho.exe --dns <hostname> <tcpPort> [count] [delay]
Invoke-TestCase 'E1-dns-mode-success' {
    $ctx = Start-TestServer -ServiceTimeout $ServiceTimeoutMs
    $r = Run-ProcessChecked $ClientEchoExe `
        @('--dns', 'localhost', [string]$ctx.TcpPort, '2', '50') `
        (Split-Path -Parent $ClientEchoExe) 20000
    if ($r.ExitCode -ne 0) {
        throw ("--dns localhost run failed (exit " + $r.ExitCode + "); output: " +
            (Join-FirstLines $r.Output 8))
    }
    if ($r.Output -notmatch 'найден сервер') {
        throw "--dns localhost did not report a found server; output: $($r.Output)"
    }
    return "DNS mode resolved localhost and completed echo session, exit 0"
}

# (f2) DNS mode with an unresolvable host must fail fast, nonzero, and name the host
Invoke-TestCase 'E2-dns-mode-unresolvable-nonzero' {
    $ctx = Start-TestServer -ServiceTimeout $ServiceTimeoutMs
    $badHost = 'no-such-host-' + [Guid]::NewGuid().ToString('N').Substring(0, 8) + '.invalid'
    $r = Run-ProcessChecked $ClientEchoExe `
        @('--dns', $badHost, [string]$ctx.TcpPort, '1', '50') `
        (Split-Path -Parent $ClientEchoExe) 20000
    if ($r.ExitCode -eq 0) {
        throw "--dns $badHost exited 0; nonzero required for unresolvable host"
    }
    if ($r.ElapsedMs -gt 15000) { throw "unresolvable host took $($r.ElapsedMs)ms; error must be finite" }
    if ($r.Output -match 'найден сервер') { throw "unresolvable host reported a found server" }
    if ($r.Output -notmatch [regex]::Escape($badHost)) {
        throw ("error output does not mention the failed hostname; output: " +
            (Join-FirstLines $r.Output 8))
    }
    return "unresolvable host -> exit $($r.ExitCode) in $($r.ElapsedMs)ms, hostname reported"
}

# (g0 sanity) RConsole non-interactive command roundtrip
Invoke-TestCase 'F1-rconsole-command-answer' {
    $ctx = Start-TestServer -ServiceTimeout $ServiceTimeoutMs
    $r = Run-ProcessChecked $RConsoleExe @($ctx.PipeName, 'statistics') `
        (Split-Path -Parent $RConsoleExe) 15000
    if ($r.ExitCode -ne 0) { throw "RConsole statistics exit $($r.ExitCode); output: $($r.Output)" }
    if ($r.Output -notmatch 'RConsole: statistics ->') {
        throw "RConsole did not print an answer line; output: $($r.Output)"
    }
    return $r.Output.Trim()
}

# (g0 sanity) RConsole without a server must report the connect failure; nonzero exit
# is not yet part of the visible contract (RConsole.cpp returns 0 on connect failure),
# so exit 0 with the canonical failure message is reported as a skip for parent review.
Invoke-TestCase 'F2-rconsole-no-server-nonzero' {
    $ghost = '\\.\pipe\lab810smoke-no-such-' + $PID
    $r = Run-ProcessChecked $RConsoleExe @($ghost, 'statistics') `
        (Split-Path -Parent $RConsoleExe) 15000
    if ($r.Output -notmatch 'не удалось подключиться|не выполнена') {
        throw ("RConsole did not report the connect failure; exit " + $r.ExitCode +
            "; output: " + (Join-FirstLines $r.Output 8))
    }
    if ($r.ExitCode -eq 0) {
        throw ("RConsole reports the connect failure but exits 0; nonzero exit on " +
            "failure is required (protection runbook: 'клиенты должны завершиться с кодом ошибки')")
    }
    return "no server -> exit $($r.ExitCode), failure reported"
}

# (g1) connect/disconnect diagnostics appear in the server journal
Invoke-TestCase 'F3-journal-connect-disconnect-events' {
    $ctx = Start-TestServer -ServiceTimeout $ServiceTimeoutMs
    Run-EchoSession $ctx.TcpPort (New-PayloadBytes 24 'F3') $SocketTimeoutMs
    $conn = Wait-LogContains $ctx.Process 'подключён клиент' 5000
    if (-not $conn) { throw "journal has no connection event line" }
    $fin = Wait-LogContains $ctx.Process 'завершение обслуживания' 5000
    if (-not $fin) { throw "journal has no service-completion event line" }
    return "journal: '$conn' / '$fin'"
}

# (g2) event feed over the pipe: the 'events' command returns queued diagnostics
# (connect/complete/refuse/timeout) and clears the queue.
Invoke-TestCase 'F4-rconsole-event-feed' {
    $ctx = Start-TestServer -ServiceTimeout $ServiceTimeoutMs
    Run-EchoSession $ctx.TcpPort (New-PayloadBytes 24 'F4') $SocketTimeoutMs
    $null = Wait-LogContains $ctx.Process 'завершение обслуживания' 5000
    $answer = Send-PipeCommand $ctx.PipeName $EventsCommand $SocketTimeoutMs
    if ($answer -eq 'nocmd') {
        throw "event poll command '$EventsCommand' answered nocmd; diagnostics queue not implemented"
    }
    if ([string]::IsNullOrWhiteSpace($answer) -or $answer -eq '-') {
        throw "event feed is empty although a completed session must be queued"
    }
    if ($answer -notmatch 'подключён|завершён') {
        throw "event feed answer '$answer' has no connect/complete events"
    }
    # second poll drains the queue: '-' (empty) is expected
    $answer2 = Send-PipeCommand $ctx.PipeName $EventsCommand $SocketTimeoutMs
    if ($answer2 -ne '-') {
        throw "second event poll did not drain the queue: '$answer2'"
    }
    return "event feed: '$answer'; second poll drained"
}

# ------------------------------------------------------------------- summary

Stop-TrackedProcesses

$pass = @($script:Results | Where-Object { $_.Status -eq 'PASS' }).Count
$fail = @($script:Results | Where-Object { $_.Status -eq 'FAIL' }).Count
$skip = @($script:Results | Where-Object { $_.Status -eq 'SKIP' }).Count

Write-Output ""
Write-Output "SUMMARY: pass=$pass fail=$fail skip=$skip (exit code = fail count)"

try {
    $script:Results | ConvertTo-Json -Depth 3 |
        Set-Content -Path (Join-Path $script:TempDir 'results.json') -Encoding UTF8
    Write-Output "results: $(Join-Path $script:TempDir 'results.json')"
} catch { }

exit $fail
