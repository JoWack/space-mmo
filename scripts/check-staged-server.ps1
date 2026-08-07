# Is the staged dedicated server older than the code it is supposed to be running?
#
# Exits 0 when it is safe to launch, 1 when it is not. Anything unexpected also exits 1: a check
# that cannot tell must refuse, because the first version of this lived inline in the batch file,
# failed to parse, produced an empty answer, and launched the stale server anyway -- which is the
# exact failure it was written to prevent.

$ErrorActionPreference = 'Stop'

$ServerExe = 'D:\Programming\SpaceMMO\client\Saved\StagedBuilds\WindowsServer\SpaceMMOServer.exe'
$SourceDir = 'D:\Programming\SpaceMMO\client\Source'

$Cook = @'
  cd /d D:\Programming\UnrealEngineSource
  Engine\Build\BatchFiles\RunUAT.bat BuildCookRun -project="D:\Programming\SpaceMMO\client\SpaceMMO.uproject" -noP4 -utf8output -platform=Win64 -serverconfig=Development -server -noclient -build -cook -stage -pak
'@

$exe = Get-Item $ServerExe -ErrorAction SilentlyContinue

if (-not $exe) {
    Write-Host ''
    Write-Host '  No staged server found at:' -ForegroundColor Red
    Write-Host "    $ServerExe"
    Write-Host ''
    Write-Host '  Cook one (about 30 minutes):'
    Write-Host $Cook
    exit 1
}

$newest =
    Get-ChildItem $SourceDir -Recurse -Include *.cpp, *.h -ErrorAction SilentlyContinue |
    Sort-Object LastWriteTime -Descending |
    Select-Object -First 1

if (-not $newest) {
    Write-Host "  Could not read any source under $SourceDir; refusing rather than guessing." -ForegroundColor Red
    exit 1
}

if ($newest.LastWriteTime -le $exe.LastWriteTime) {
    exit 0
}

Write-Host ''
Write-Host '  REFUSING TO START: the staged server is older than your code.' -ForegroundColor Red
Write-Host ''
Write-Host ("    newest source:  {0}  ({1})" -f $newest.Name, $newest.LastWriteTime)
Write-Host ("    staged server:  {0}" -f $exe.LastWriteTime)
Write-Host ''
Write-Host '  Running it anyway gives a server that disagrees with the client. A changed replicated'
Write-Host '  property shows up as "FieldCache == nullptr" in the client log; a newly added component'
Write-Host '  shows up as nothing at all, because the server has never heard of the action being sent.'
Write-Host '  The second kind cost a session on the docking component.'
Write-Host ''
Write-Host '  Re-cook (about 30 minutes):'
Write-Host $Cook

exit 1
