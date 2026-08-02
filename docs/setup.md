# Development environment setup

Detected state of this machine as of 2026-07-29:

| Tool | Status |
|---|---|
| Git 2.43 | ✅ installed |
| Git LFS 3.4 | ✅ installed |
| Node 20.10 | ✅ installed (not needed yet) |
| Python 3.12.1 | ✅ installed (not needed yet) |
| .NET SDK 10.0.302 | ✅ installed — solution builds, 176 tests pass |
| Docker Desktop 29.6.2 | ✅ installed — Postgres 17.10 running and healthy |
| VS Build Tools 2022 (MSVC 14.44) | ✅ installed — this is what compiles UE C++ |
| Windows SDK 10.0.26100 | ✅ installed |
| Unreal Engine 5.8.1 | ✅ `D:\Programming\UnrealEngine\UE_5.8` |
| .NET Framework SDK 4.8 | ✅ installed — editor builds, 96 automation tests pass |
| UE source build | ❌ **required for any dedicated server target** — see below |

**Everything M1 needs is installed**, and both the UE game and editor targets compile with the
96 automation tests passing. One thing still blocks part of M2: dedicated server targets
need a source build of Unreal. See §2.

---

## 0. .NET SDK — already done

The repo targets `net10.0` and SDK 10.0.302 is installed. Verify at any time with:

```bash
dotnet test services/SpaceMMO.Server.sln
```

**If you ever move to a different SDK version**, change one line —
`<TargetFramework>` in `Directory.Build.props` at the repo root — rather than editing every
`.csproj`. That file exists for exactly this reason.

## 1. Docker Desktop — already done

Installed and verified: Postgres 17.10 comes up healthy in about a second, with `C`
collation applied as intended.

```bash
cp infra/.env.example infra/.env && docker compose -f infra/docker-compose.yml up -d
```

```bash
docker compose -f infra/docker-compose.yml ps
```

`STATUS` should read `healthy`. If it says `starting` for more than about 30 seconds,
check `docker compose -f infra/docker-compose.yml logs postgres`.

### ⚠️ Docker is a per-user install on this machine

Docker Desktop installed to `%LOCALAPPDATA%\Programs\DockerDesktop`, **not** to
`C:\Program Files\Docker`. The CLI therefore lives at:

```
C:\Users\Joe\AppData\Local\Programs\DockerDesktop\resources\bin
```

The installer adds that to the *user* PATH, but **already-open shells keep the
environment they started with**. So `docker: command not found` right after installing
means the shell is stale, not that anything is broken — restart the terminal.

To confirm the daemon is actually up rather than just the CLI being present:

```bash
docker info --format "{{.ServerVersion}}"
```

### Checking the database by hand

```bash
docker exec spacemmo-postgres psql -U spacemmo -d spacemmo -c "select version();"
```

Note that `SHOW lc_collate` no longer works on Postgres 17 — it stopped being a runtime
parameter. Collation is per-database now:

```bash
docker exec spacemmo-postgres psql -U spacemmo -d spacemmo -c "select datcollate from pg_database where datname='spacemmo';"
```

That must return `C`. If it returns anything else, the volume was created before
`POSTGRES_INITDB_ARGS` was set — `initdb` only runs on an empty volume, so the fix is
`docker compose -f infra/docker-compose.yml down -v` to destroy the data and let it
re-initialize.

**Alternative if you'd rather not run Docker:** a native Postgres 17 install works
identically — `winget install PostgreSQL.PostgreSQL.17`. Match the credentials in
`infra/.env.example`, and initialize the cluster with `--locale=C` to match the
compose file, or `ORDER BY` results will differ between your machine and CI.

## 2. Unreal toolchain

The C++ compiler is **Visual Studio Build Tools 2022**, not VS Code. The C/C++ extension only
provides IntelliSense and debugging; UnrealBuildTool finds MSVC through `vswhere` and Build
Tools registers there, so the full Visual Studio IDE is not needed.

**Verified working:** the game target compiles.

```bash
"/d/Programming/UnrealEngine/UE_5.8/Engine/Build/BatchFiles/Build.bat" SpaceMMO Win64 Development -Project="D:\Programming\SpaceMMO\client\SpaceMMO.uproject"
```

### The editor target needs the .NET Framework SDK — resolved

`SpaceMMOEditor` initially failed with:

```
Unable to instantiate module 'SwarmInterface': Could not find NetFxSDK install dir
```

`SwarmInterface` is editor-only and needs .NET Framework SDK 4.6+. Installing the
**.NET Framework 4.8 SDK** and **targeting pack** through the Visual Studio Installer fixed it.
NETFXSDK 4.8 is now present and the editor builds.

### Seeding a database

A migrated database is still empty, and an empty database cannot create a character — the four
race homeworlds live in `data/universe/`, not in a migration. Apply migrations and load all
authored content with:

```bash
dotnet run --project services/SpaceMMO.Api -- --seed
```

Seeding is a separate command rather than something startup does, on purpose. A server that
migrates and rewrites content on every boot will eventually do that to a production database
during an unrelated restart.

### After cooking, the project belongs to the source engine

`BuildCookRun` rebuilds `SpaceMMOEditor` against the **source** engine and writes it into
`client/Binaries/Win64` — the same place the launcher engine's build went. From then on the
launcher engine cannot run the project at all, and fails with:

> The game module 'SpaceMMOCore' could not be found. Please ensure that this module exists and
> that it is compiled.

That message is misleading: the module exists and is compiled, just against a different engine.
**Once you have cooked, run everything from the source tree** — automation tests, PIE clients,
all of it. Mixing the two engines was never going to work for networking anyway, since a client
and server built from different engines disagree on the network version.

### Two clients on the dedicated server

```bash
cd /d/Programming/SpaceMMO/client/Saved/StagedBuilds/WindowsServer && ./SpaceMMOServer.exe -log -unattended -nopause -port=7777
```

Then two clients, from the **source** engine's editor:

```bash
"/d/Programming/UnrealEngineSource/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "D:/Programming/SpaceMMO/client/SpaceMMO.uproject" 127.0.0.1:7777 -game -unattended -nopause -nosplash -nullrhi -log=ClientA.log
```

Expect `Welcomed by server` on each client and one `Join succeeded` per client on the server.
Each client logs `Ship ready` twice — its own ship and the other player's, replicated in. The
server logs two as well and flies neither: a dedicated server owns the simulation and has no pawn
of its own. Allow about seventy seconds for both clients to reach the server on a cold start,
which is long enough that an impatient timeout reads as a hang.

### Two traps when running a dedicated server

**Re-cook after every code change.** Clients are built from current source; the staged server is a
snapshot. If a replicated property has been added or removed since it was cooked, the client log
fills with `ReceivedBunch: FieldCache == nullptr` and `GetFromIndex failed` and the session is
unusable. That is a stale server rather than a network problem, and adding a single UPROPERTY is
enough to cause it.

**Scenery is built by every machine, not replicated.** `USpaceMMOWorldSubsystem` spawns the test
scene, the planet and the lights in `OnWorldBeginPlay`, which runs on the server and on every
client. It deliberately does not live in the game mode: a game mode exists only on the server, so
anything it spawns is invisible to clients — which showed up as a joining player staring at a
black screen with nothing but a couple of ship pawns in it and no light to see them by.

Replicating it would be the wrong fix. The scene is a deterministic function of its configuration,
so both ends build an identical copy for free rather than sending three hundred and fifty marker
cubes over the wire to say something they both already know.

### "The planet is running away from me"

It is not. Planets are fixed points in system space and nothing ever moves them; this was measured
by flying at one and logging both the true distance and the drawn distance every second, and they
track exactly while the planet's world position steps 20 km closer on each rebase.

What it really was: the demo planet sat 200 km away while the ship accelerated at 2,000 cm/s²
toward a top speed of 200,000. Reaching that speed alone took a hundred seconds and a hundred
kilometres, so the trip was about two and a half minutes of unbroken thrust — during which a
distant sphere barely changes apparent size while the marker lattice streams past three kilometres
apart. Everything nearby looks fast and the destination looks static, which reads as the planet
running away.

Fixed by tuning rather than by physics: the planet is now 60 km out and thrust is 20,000 cm/s², so
top speed arrives in ten seconds and the approach takes well under a minute.

Two flags help when investigating anything like this:

```bash
scripts\play.bat -NoFlightAssist -LogApproach -ShipVelX=200000
```

`-LogApproach` prints the true and drawn distance to the planet once a second, which is what
settles "is it moving?" in one run. `-NoFlightAssist` zeroes the damping, without which an
injected velocity bleeds off within about five kilometres.

### Playing it by hand

Scripts live in `scripts/`. All of them use the **source** engine, which is mandatory: once
BuildCookRun has run, the project's binaries are compiled against `UnrealEngineSource` and the
launcher engine can no longer load them.

| Script | What it does |
|---|---|
| `play.bat` | Standalone single player in a window. Needs nothing else running. |
| `editor.bat` | Opens the editor. Press Play for the same thing with tooling attached. |
| `host-dedicated.bat` | The cooked dedicated server on port 7777. |
| `join.bat [log]` | Connects a client to it. Run twice, with different log names, for two players. |
| `api.bat` | The backend on port 5080. Not needed for flight, terrain or walking. |

**No backend is required for gameplay.** Flight, planetary approach, terrain streaming, landing,
walking and boarding all run client-side today; the API is only involved in accounts and
characters, which nothing in the world calls yet.

`play.bat` forwards extra arguments, so the dev flags compose:

```bash
scripts\play.bat -ShipStartX=178 -ShipStartY=0 -ShipStartZ=0 -AutoDisembark
```

**Never pass `-nullrhi` when playing.** It disables rendering entirely — it is for headless test
runs, and with it you get a process and no window.

#### Controls

| | Ship | On foot |
|---|---|---|
| `W` `S` | Thrust forward / back | Walk forward / back |
| `A` `D` | Thrust left / right | Strafe |
| `Mouse` | Pitch and yaw | Turn |
| `Q` `E` | Roll | — |
| `Space` | Thrust up | Jump |
| `Ctrl` | Thrust down | — |
| `Shift` | Boost | — |
| `C` | Toggle first/third person | Toggle first/third person |
| `F` | Step out (only when landed) | Board a ship within 50 m |

The ship starts 200 km from the planet, which is a long flight — use `-ShipStartX=178` to begin
just above the surface instead. Flight assist is on by default, so releasing the stick slows you
down rather than coasting.

### Seeing terrain stream in

The ship starts 200 km from the planet, which is a two-minute flight before anything terrain-
related happens. Skip it:

```bash
"/d/Programming/UnrealEngineSource/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "D:/Programming/SpaceMMO/client/SpaceMMO.uproject" -game -unattended -nopause -nosplash -nullrhi -ShipStartX=175 -ShipStartY=0 -ShipStartZ=0
```

The planet sits at 200 km with a 20 km radius and a 12 km atmosphere, so 175 km puts the ship
25 km from the centre — inside the shell where terrain streams. Expect:

```
Terrain patch at (179.657, 0.000, 0.000) km: 8192 triangles across 10.0 degrees.
```

Start at 178 instead and the ship falls the last two kilometres under gravity and lands:

```
Touched down at (179.637, 0.000, 0.000) km
```

That is the ground at 179.657 minus the ship's own 0.02 km hull radius, which is what resting on
a surface should look like.

Three separate scalars rather than one comma-separated vector, because **`FParse::Value` treats a
comma as a delimiter** and returns only the first component. That fails silently and looks
identical to the flag being ignored, which is a genuinely annoying twenty minutes.

### Standing a character on the planet

```bash
"/d/Programming/UnrealEngineSource/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "D:/Programming/SpaceMMO/client/SpaceMMO.uproject" -game -unattended -nopause -nosplash -nullrhi -SpawnCharacter -CharacterDirX=0 -CharacterDirY=0 -CharacterDirZ=-1
```

`-CharacterDir*` is a direction from the planet centre, so it picks a point on the sphere. The
south pole is the one worth trying, because a character there is upside down in world space:

```
Character ready at (200.000, 0.000, -20.267) km, up V(X=0.00, Y=-0.00, Z=-1.00)
```

Spawned deferred, with the position set **before** FinishSpawning. BeginPlay resolves the ground
and aligns the character to it, so a position applied afterwards is too late and the character
spends its first frame somewhere else — the same ordering trap the planet actor hit.

### Descend, land, and step out

```bash
"/d/Programming/UnrealEngineSource/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "D:/Programming/SpaceMMO/client/SpaceMMO.uproject" -game -unattended -nopause -nosplash -nullrhi -ShipStartX=178 -ShipStartY=0 -ShipStartZ=0 -AutoDisembark
```

`-AutoDisembark` steps out the instant the ship settles, so the whole sequence runs without a
human holding a key:

```
Terrain patch at (179.657, 0.000, 0.000) km: 8192 triangles across 10.0 degrees.
Touched down at  (179.637, 0.000, 0.000) km
Character ready at (179.637, 0.030, 0.002) km, up V(X=-1.00, Y=-0.00, Z=-0.00)
```

Thirty metres to the side and two above, which is exactly the step-out offset. In an interactive
session the key is **F**, for both directions.

### Proving the client and server actually talk

Fixtures and integration tests each cover one side of the wire and neither proves the two agree.
This drives the real `USpaceMMOBackendClient` across a real socket. Start the API, then:

```bash
"/d/Programming/UnrealEngine/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "D:\Programming\SpaceMMO\client\SpaceMMO.uproject" -game -unattended -nopause -nosplash -nullrhi -BackendSmokeTest -BackendUrl=http://localhost:5080 -BackendEmail=you@example.com -BackendPassword=your-password
```

Results appear in `client/Saved/Logs/SpaceMMO.log` tagged `SMOKE:`. The token is never logged.

**Wait for an outcome line, not a fixed delay.** The request is issued during subsystem init,
before the engine loop ticks the HTTP manager, and the editor spends ten-plus seconds loading
plugins after that. A short sleep kills the process with the request still in flight and reports
nothing at all — no success, no failure.

### The dedicated server

Built against the **source** engine, not the launcher one — the launcher build ships zero Server
targets and cannot produce this at all.

```bash
cd /d/Programming/UnrealEngineSource && ./Engine/Build/BatchFiles/Build.bat SpaceMMOServer Win64 Development -Project="D:\Programming\SpaceMMO\client\SpaceMMO.uproject" -WaitMutex -NoUBA
```

**A dedicated server needs cooked content.** Run straight against the raw `.uproject` it asserts
in `BufferReader.h` reading a package summary, before any game code executes — engine content is
fine and `-noasyncloadingthread` does not help. Cook and stage instead:

```bash
cd /d/Programming/UnrealEngineSource && ./Engine/Build/BatchFiles/RunUAT.bat BuildCookRun -project="D:\Programming\SpaceMMO\client\SpaceMMO.uproject" -noP4 -utf8output -platform=Win64 -serverconfig=Development -server -noclient -build -cook -stage -pak
```

That also rebuilds `SpaceMMOEditor` against the source engine, which cooking requires. Then run
the staged server:

```bash
./SpaceMMOServer.exe -log -unattended -nopause -port=7777
```

A healthy start reaches `IpNetDriver listening on port 7777` and `Bringing World
/Engine/Maps/Entry.Entry up for play (max tick rate 30)`, with `Game class is 'SpaceMMOGameMode'`.
Point it at a backend with `-BackendUrl=`; it defaults to `http://localhost:5000`.

Keep the server target compiling even before it is needed. It exists to catch client-only code
before it becomes load-bearing, and it earned that on its first build:
`ADirectionalLight::GetComponent` does not exist in a server configuration, so scene lighting had
to move behind `#if !UE_SERVER`. A runtime check could not have helped — code has to compile
before it can decide not to run.

### Proving the client and server actually talk

Fixtures and integration tests each cover one side of the wire and neither proves the two agree.
This drives the real `USpaceMMOBackendClient` across a real socket. Start the API, then:

```bash
"/d/Programming/UnrealEngine/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "D:\Programming\SpaceMMO\client\SpaceMMO.uproject" -game -unattended -nopause -nosplash -nullrhi -BackendSmokeTest -BackendUrl=http://localhost:5080 -BackendEmail=you@example.com -BackendPassword=your-password
```

Results appear in `client/Saved/Logs/SpaceMMO.log` tagged `SMOKE:`. The token is never logged.

**Wait for an outcome line, not a fixed delay.** The request is issued during subsystem init,
before the engine loop ticks the HTTP manager, and the editor spends ten-plus seconds loading
plugins after that. A short sleep kills the process with the request still in flight and reports
nothing at all — no success, no failure.

### The dedicated server

Built against the **source** engine, not the launcher one — the launcher build ships zero Server
targets and cannot produce this at all.

```bash
cd /d/Programming/UnrealEngineSource && ./Engine/Build/BatchFiles/Build.bat SpaceMMOServer Win64 Development -Project="D:\Programming\SpaceMMO\client\SpaceMMO.uproject" -WaitMutex -NoUBA
```

**It compiles and links, but does not yet run.** Launched against the raw `.uproject` it asserts
in `BufferReader.h` while a background thread reads a package summary, before any game code runs.
Engine content is intact (5,247 valid packages) and `-noasyncloadingthread` does not help, so the
working theory is that a dedicated server needs **cooked** content — `RunUAT BuildCookRun -server
-noclient` — rather than the uncooked tree the editor reads. Unverified; do not treat it as fact.

Keep the target compiling even while it cannot run. It exists to catch client-only code before it
becomes load-bearing, and it earned that on its first build: `ADirectionalLight::GetComponent` does
not exist in a server configuration, so the scene lighting had to move behind `#if !UE_SERVER`.

### Running the automation tests

```bash
"/d/Programming/UnrealEngine/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "D:\Programming\SpaceMMO\client\SpaceMMO.uproject" \
  -ExecCmds="Automation RunTests SpaceMMO" \
  -testexit="Automation Test Queue Empty" \
  -unattended -nopause -nosplash -log
```

Three flag details, each of which cost a debugging cycle:

- **`-testexit` is required.** Putting `Quit` in `-ExecCmds` exits as soon as tests are *queued*,
  so the editor shuts down before any run — and exits 0, reporting success having done nothing.
- **`-nullrhi` crashes UE 5.8** on a `TNotNull` assertion immediately after engine init. It is the
  obvious flag for headless runs and it does not work.
- **`-NoShaderCompile` trips `Assertion failed: AllowShaderCompiling()`.** The editor must be able
  to compile shaders even when nothing is rendered.

The project also needs `Config/DefaultEngine.ini` with a `[/Script/EngineSettings.GameMapsSettings]`
section. Without a default map the editor initialises fully and then dies on a null assertion,
which looks nothing like missing configuration.

### If the compiler runs out of memory

```
c1xx: error C3859: Failed to create virtual memory for PCH
c1xx: fatal error C1076: compiler limit: internal heap limit reached
```

Not a code error. UnrealBuildTool defaults to one compile process per physical core — eight here —
and Unreal's precompiled headers are large enough that eight at once can exhaust the compiler's
address space on a 32 GB machine, especially with the editor, Docker and Postgres also resident.

```bash
"/d/Programming/UnrealEngine/UE_5.8/Engine/Build/BatchFiles/Build.bat" SpaceMMOEditor Win64 Development -Project="D:\Programming\SpaceMMO\client\SpaceMMO.uproject" -MaxParallelActions=3
```

Slower, but it completes. Worth reaching for immediately rather than hunting for a compile error
that is not there.

### ⚠️ Dedicated servers need a source build of Unreal

```
Server targets are not currently supported from this engine distribution.
```

**This corrects earlier guidance in this document.** A source build is not only needed for Linux
cross-compilation — the launcher binary distribution cannot build *any* dedicated server target,
including Windows. Since ADR-0003 puts one dedicated server per star system at the centre of the
architecture, a source build from GitHub becomes necessary before M2's networking work.

It is a large undertaking — a linked Epic GitHub account, roughly 100 GB, and hours of
compilation — so it is worth starting well before it blocks anything. It is not needed for the
coordinate and flight work that comes first.

## 3. Visual Studio 2022 — optional

UE 5.x requires **VS 2022**; VS 2019 will not work. Community edition is sufficient.

Required workloads:

- **Desktop development with C++**
- **Game development with C++**
- **ASP.NET and web development** (this also supplies the .NET SDK from step 1)

Required individual components — these are the ones the workloads miss and whose
absence produces confusing UE build errors:

- MSVC v143 build tools (x64/x86)
- Windows 10/11 SDK (latest)
- C++ address sanitizer
- .NET Framework 4.8 SDK — needed by UnrealBuildTool

```bash
winget install Microsoft.VisualStudio.2022.Community
```

Workloads still have to be selected in the installer UI after that.

## 4. Unreal Engine 5 — installed

The Epic Games Launcher is already installed on this machine.

**Install the latest stable 5.x** (verify the current version in the launcher's
Unreal Engine → Library tab; do not use a Preview build).

> ⚠️ **Install to `D:`.** Disk state on this machine: `D:` has ~435 GB free, but `G:`
> and `H:` are at 98% and `E:` is at 93%. UE with debug symbols wants 100+ GB, and the
> DerivedDataCache grows well beyond the install size during development.

In the launcher, use the dropdown beside Install to set the location, and under
**Options** select:

- ✅ Starter Content — needed for M2 placeholder assets
- ✅ Engine Source — enables debugging into engine code, which you will want
- ❌ Editor symbols for debugging — ~40 GB; skip until you actually need engine
  stack traces
- ❌ All target platforms except Windows

UE 5.8.1 is installed to `D:\Programming\UnrealEngine\UE_5.8`. The launcher build is enough for
the client and for all the coordinate and flight work in M2 — but **not** for dedicated servers,
which need a source build. See §2.

---

## Working with the repo

```bash
dotnet test services/SpaceMMO.Server.sln                    # run all tests
dotnet run --project tools/SpaceMMO.EconSim                 # run the economy sim
docker compose -f infra/docker-compose.yml up -d            # start Postgres
docker compose -f infra/docker-compose.yml down             # stop, keeping data
```

### Git LFS

LFS is already initialized in this repo. Before committing any UE assets, confirm the
filters are active — a missed LFS filter bloats history permanently and is painful to
undo:

```bash
git lfs status
```

`.gitattributes` already routes `*.uasset`, `*.umap`, and source art through LFS.
Content data (`data/**/*.json`) is deliberately **not** in LFS, so balance changes stay
reviewable as diffs.

### A note on the current state

The .NET projects were hand-authored rather than generated with `dotnet new`, because no
SDK was installed at the time. They are ordinary MSBuild files and build normally:
**176 tests pass** as of the last run.

One wrinkle worth knowing about. `services/Directory.Build.props` sets
`TreatWarningsAsErrors` together with `AnalysisLevel=latest-recommended`, which is
deliberate for a codebase handling player wealth — a nullability warning there is a
future exception inside a market transaction. It does mean analyzer rules block the
build, and one had to be suppressed for tests: `CA1707` forbids underscores in member
names, but `Method_Scenario_Expectation` is the conventional xUnit style. It is
suppressed in the test `.csproj` only, never in production projects. Expect to make
similar narrow, justified suppressions rather than relaxing the global setting.
