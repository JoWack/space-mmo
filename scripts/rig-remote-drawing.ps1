# How other players are drawn, measured without anyone at the keys (task 182).
#
# Starts the staged dedicated server on its own port, so a server already running on 7777 is left alone,
# and three headless clients that sign in as nobody (the backend at a dead port):
#   RigA  walks by itself (SpaceMMO.AutoWalk): a wide circle, sprinting every other lap, stopping a
#         second in eight;
#   RigB  draws the others the old way (SpaceMMO.RemoteSmoothing 0);
#   RigC  draws them the new way (SpaceMMO.RemoteSmoothing 1, the default).
# Each client logs a REMOTE: line a second per other player: updates and their gaps, how far each moved
# the projection, how far the drawing was from it, how evenly it moved, the biggest turn in a frame.
# Read them in client\Saved\Logs\RigB.log and RigC.log; the walker is the pawn whose updates move it.
#
# That is where they are drawn, not how they move. What their animation is handed -- ground speed, vertical
# speed, direction, the up those are measured against -- is in the DRAW: lines, and
# scripts\compare-remote-draw.py pairs the walker's own with each copy's. A copy reading a run as a glide
# sat in these logs on 5 October, unread, until Joe saw it (task 182).
#
#   powershell -ExecutionPolicy Bypass -File scripts\rig-remote-drawing.ps1 [-Seconds 75] [-A '...'] [-B '...'] [-C '...']
#
# -A, -B and -C replace each client's console commands, e.g. -A 'SpaceMMO.AutoWalk 1, SpaceMMO.ForceBodyRace 3'
# to check every client sees another's race. The staged server must carry the replicated layout the clients
# were built with: any change to a replicated property or an RPC needs a re-cook first (CLAUDE.md).
param(
    [int]$Seconds = 75,
    [string]$A = 'SpaceMMO.AutoWalk 1',
    [string]$B = 'SpaceMMO.RemoteSmoothing 0',
    [string]$C = 'SpaceMMO.RemoteSmoothing 1'
)
$ErrorActionPreference = 'Stop'
$editor = 'D:\Programming\UnrealEngineSource\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$server = 'D:\Programming\SpaceMMO\client\Saved\StagedBuilds\WindowsServer\SpaceMMOServer.exe'
$project = '"D:\Programming\SpaceMMO\client\SpaceMMO.uproject"'
$logs = 'D:\Programming\SpaceMMO\client\Saved\Logs'
foreach ($f in 'RigA.log', 'RigB.log', 'RigC.log') { if (Test-Path "$logs\$f") { Remove-Item "$logs\$f" } }

$procs = @()
$procs += Start-Process -FilePath $server -PassThru -WindowStyle Hidden -ArgumentList @('-port=7788', '-log=RigServer.log', '-unattended')
Start-Sleep -Seconds 12

function Client($name, $cmds) {
    # -ExecCmds survives this way, as an argument array to Start-Process (scripts\tests.ps1 does the same);
    # check LogInit: Command Line in the client's log before believing it arrived.
    Start-Process -FilePath $editor -PassThru -WindowStyle Hidden -ArgumentList @(
        $project, '127.0.0.1:7788', '-game', '-nullrhi', '-unattended', '-nopause', '-nosplash',
        '-BackendUrl=http://localhost:9', "-log=$name.log", "-ExecCmds=`"$cmds`"")
}

$procs += Client 'RigA' $A
$procs += Client 'RigB' $B
$procs += Client 'RigC' $C
"started: " + (($procs | ForEach-Object { $_.Id }) -join ', ')
Start-Sleep -Seconds $Seconds

# By process ID, never by image name: Joe's own editor, clients and server may be running. With the tree:
# the staged SpaceMMOServer.exe is a launcher, and stopping it alone left the real server running on 7788
# (5 October), where the next cook would have found its executable locked.
foreach ($p in $procs) { & taskkill.exe /PID $p.Id /T /F 2>$null | Out-Null }
"stopped; logs in $logs\RigA.log, RigB.log, RigC.log"
