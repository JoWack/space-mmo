<#
.SYNOPSIS
Runs the client automation suite and fails loudly when it does not finish.

.DESCRIPTION
Task 113. The suite occasionally stopped one or two tests from the end with exit code 0, no
failures and no completion line -- indistinguishable from success if you count passes.

The cause is the flag that ends the run. Two options exist and each is broken in its own way:

  * -testexit is a log watcher (FOutputDeviceTestExit, LaunchEngineLoop.cpp:397). It substring
    matches every log line, from any thread, and the next engine tick calls
    RequestExit(Force=true): no clean shutdown, no guaranteed flush. Truncation at the tail is
    exactly what that produces. It also leaves GIsCriticalError unset, so the process exits 0 even
    when tests fail (AutomationCommandline.cpp:491-504) -- the exit code never meant anything.

  * 'Automation SoftQuit' shuts down gracefully, so the log is complete and the completion line is
    written before shutdown begins. Measured 18 August: it then hangs partway through editor
    shutdown and never exits. Its exit code is therefore unavailable too.

So this script takes the complete log and supplies the missing terminator itself: it waits for the
completion line, then kills the editor. The log is the source of truth and the script's own exit
code is the contract -- the editor's is not trustworthy under either flag.

Plain 'Quit' in -ExecCmds is the engine's quit console command rather than the automation one, which
is why it exits before any test runs. That is what -testexit was originally reached for.

.PARAMETER Filter
Test prefix to run. Defaults to the whole project suite.

.PARAMETER Expected
Assert this many tests ran. Omit to accept any number, but report it.

.PARAMETER TimeoutMinutes
How long to wait for the completion line before giving up. A full suite takes a few minutes;
shader work after a header change can double it.
#>
param(
    [string] $Filter = 'SpaceMMO',
    [int] $Expected = 0,
    [int] $TimeoutMinutes = 30
)

$ErrorActionPreference = 'Stop'

# The bat wrapper passes both through even when empty, so treat blank as unset rather than failing
# on a conversion the caller never asked for.
if ([string]::IsNullOrWhiteSpace($Filter)) { $Filter = 'SpaceMMO' }

$editor = 'D:\Programming\UnrealEngineSource\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$project = 'D:\Programming\SpaceMMO\client\SpaceMMO.uproject'
$log = 'D:\Programming\SpaceMMO\client\Saved\Logs\SpaceMMO.log'

if (-not (Test-Path $editor)) { throw "No editor at $editor" }
if (-not (Test-Path $project)) { throw "No project at $project" }

# Removed first, so a run that dies before writing anything cannot be read as the previous run's
# result. Reading a stale log is the failure this script exists to prevent.
if (Test-Path $log) { Remove-Item $log -Force }

# Quoted here, inside the string, rather than left to Start-Process. Its own quoting drops these
# and the value arrives split on spaces: -ExecCmds=Automation, then RunTests and the rest as
# separate tokens. The editor then starts, reports "Ready to start automation", queues nothing, and
# sits there indefinitely -- a run that looks like a slow one rather than a broken command line.
#
# Verified by reading LogInit: Command Line: in the log, which is the only thing that settles
# whether a flag arrived on this machine.
$command = '-ExecCmds="Automation RunTests {0}; Automation SoftQuit"' -f $Filter

$process = Start-Process -FilePath $editor -PassThru -WindowStyle Hidden -ArgumentList @(
    "`"$project`"", $command, '-unattended', '-nopause', '-nosplash', '-log')

$deadline = (Get-Date).AddMinutes($TimeoutMinutes)
$finished = $false

while ((Get-Date) -lt $deadline) {
    if (Test-Path $log) {
        # Opened share-all: the editor holds the log open for writing the whole time, and a plain
        # read fails on the lock rather than returning what has been written so far.
        $stream = New-Object System.IO.FileStream($log, 'Open', 'Read', 'ReadWrite')
        $reader = New-Object System.IO.StreamReader($stream)
        $sofar = $reader.ReadToEnd()
        $reader.Close()
        $stream.Close()

        if ($sofar -match 'Queue Empty \d+ tests performed') { $finished = $true; break }
    }

    if ($process.HasExited) { break }

    Start-Sleep -Seconds 5
}

if (-not $process.HasExited) {
    # Supplying the terminator SoftQuit does not. The tests are over and the log is written by this
    # point; what remains is editor shutdown, which is where it hangs.
    Stop-Process -Id $process.Id -Force
    $process.WaitForExit(30000) | Out-Null
}

if (-not (Test-Path $log)) { throw 'The run wrote no log at all.' }

$text = Get-Content $log -Raw

# What the editor was actually told, not what it was meant to be told. A mangled -ExecCmds produces
# an editor that idles forever with an empty queue, which reads as a slow suite rather than a
# broken invocation -- and it cost most of an hour before this check existed.
$sent = [regex]::Match($text, 'LogInit: Command Line:([^
]*)')

if ($sent.Success -and $sent.Groups[1].Value -notmatch 'RunTests') {
    Write-Host 'FAIL: the editor never received a RunTests command. Command line was:'
    Write-Host "  $($sent.Groups[1].Value.Trim())"
    exit 1
}

$complete = [regex]::Match($text, 'Queue Empty (\d+) tests performed')
$failures = [regex]::Matches($text, 'Result=\{Fail')

$problems = @()

# The load-bearing assertion. Without this line the run stopped early, whatever else it says.
if (-not $complete.Success) {
    $problems += 'no completion line: the run stopped before the queue emptied'
}

$count = 0
if ($complete.Success) { $count = [int] $complete.Groups[1].Value }

if ($Expected -gt 0 -and $complete.Success -and $count -ne $Expected) {
    $problems += "ran $count tests, expected $Expected"
}

if ($failures.Count -gt 0) { $problems += "$($failures.Count) test(s) failed" }

if ($problems.Count -eq 0) {
    Write-Host "PASS: $count tests, 0 failures."
    exit 0
}

Write-Host "FAIL: $($problems -join '; ')"

foreach ($line in [regex]::Matches($text, 'Result=\{Fail\}[^\r\n]*')) {
    Write-Host "  $($line.Value)"
}

# Kept for the next occurrence rather than described. A truncated run leaves no error to read, so
# the last thing it managed to say is the only evidence there is.
if (-not $complete.Success) {
    $kept = Join-Path $env:TEMP 'spacemmo-truncated-run.log'
    Copy-Item $log $kept -Force
    Write-Host "  Truncated run saved to $kept"
    Write-Host '  Last lines:'
    Get-Content $log -Tail 12 | ForEach-Object { Write-Host "    $_" }
}

exit 1
