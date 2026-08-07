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

### The project is pinned to the source build by GUID

`SpaceMMO.uproject` names its engine as `{76471CDA-4509-21F4-9199-24965F66CD1C}`, which is the
source tree's entry under `HKCU\Software\Epic Games\Unreal Engine\Builds`.

It used to say `"5.8"`. That is a *version* rather than a build, so the project resolved to
whichever 5.8 engine was registered — and the Epic launcher registers itself as one. Both engines
are 5.8.1, so this never looked like a version problem: what differed was the build. UBT stamps a
build id into `Binaries/Win64/*.modules` recording which engine compiled the DLLs, so each engine
found the other's binaries foreign and offered to rebuild. Accepting invalidated them for the
other one, and the two ping-ponged forever.

**The GUID is specific to this machine.** A fresh clone, a reinstalled source build, or a second
computer will have a different one, and the project will refuse to open until it is updated —
right-click the `.uproject`, *Switch Unreal Engine version*, pick the source build. Pinning is
still worth it: the alternative relies on remembering never to open the project from the launcher,
and this rule is load-bearing enough that it should be enforced rather than recalled.

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

### Where terrain is, and where it is not

Terrain only exists inside the atmosphere. The demo planet is 20 km in radius with a 12 km
atmosphere, so the patch appears once the viewer is within **32 km of the planet's centre** — about
twelve kilometres above the ground — and is released again on the way out. Further away there is
only the smooth sphere, which is all a planet needs to be at that distance.

The patch is **1.4 km across**, so from high altitude it is a small square on an otherwise smooth
world, and on foot it is simply the ground. Both are correct; it looks like a floating square
because it is one.

**That visible edge is the cost of the landing-zone approach**, not a bug. Terrain rises up to
500 m above the nominal radius while the sphere mesh sits exactly at it, so the patch stands proud
of the sphere and its rim is a cliff. Tapering the rim down to meet the sphere would hide it and
must not be done: the mesh would then disagree with the height function the server uses, and
players would stand on ground the server believes is somewhere else. `SpaceMMO.Patch.SitsOnTheTerrain`
exists to catch exactly that. The real fix is full cube-sphere LOD, which is the deliberately
deferred half of the terrain plan.

Cheap ways to make the edge less obvious in the meantime: raise `PatchAngularRadiusDegrees` so the
rim is further away, or lower `MaxElevationKilometres` so the step down is smaller.

### Why the scene is lit the way it is

Two things that are easy to get wrong in a scene made almost entirely of empty space, both of
which produced a planet rendered as one white hemisphere and one black one.

**Auto-exposure is off** (`r.DefaultFeature.AutoExposure=False` in `DefaultEngine.ini`). It
measures scene brightness and adapts, and in space almost everything is black, so it opens all the
way and saturates every lit surface to white.

**Fill comes from a second directional light, not a sky light.** A sky light captures its
surroundings to produce ambient, and there is no sky, no atmosphere and no horizon out here to
capture — it faithfully captured black and scaled it, contributing exactly nothing. Anything
facing away from the key light was therefore at zero. The fill is dim, cool, aimed from roughly
the opposite side, and casts no shadows: two shadow-casting suns on a sphere produce crossing
terminators that read as a rendering fault.

A sky light becomes the right tool again the moment there is a skybox or atmosphere to capture.

**Both lights are tunable live.** Lighting can only be judged by looking at it, and a rebuild is
several minutes, so press the console key in a running game and type:

```
SpaceMMO.KeyLight 20
SpaceMMO.FillLight 4
```

The change applies on the next frame. Directional light intensity is in **lux**, which is an
unforgiving unit: 3 is roughly twilight, which is why the scene went dark the moment auto-exposure
stopped compensating for it. Raise the key until the lit side looks right, then the fill until the
dark side is dim rather than absent. Once the numbers are settled they belong in the defaults at
the top of `SpaceMMOWorldSubsystem.cpp`.

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
| `join.bat a`\|`b` | Connects a client. `a` and `b` are two different players; no letter is the default login. |
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
| `M` | Release the mouse | Release the mouse |

Everything the backend does has a key too. These are the ones that were missing from this
table for long enough that they got guessed at in conversation and guessed wrong — `E`, not
`G`, is gather:

| Key | Does |
|---|---|
| `Tab` | Show or hide the character panel: skills, holdings, quests, market, industry, jobs |
| `E` | Gather from the deposit you are standing at |
| `J` | Accept the next available quest |
| `R` `X` `Z` | Industry: cycle recipe, start the job, claim it when it finishes |
| `H` `N` `B` | Market: cycle which holding, list ten of it, buy the best ask |
| `V` | Sell a parcel to the faction standing order — the worst price in the game, by design |

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

### The service credential

The Unreal dedicated server acts on players' behalf when gathering, so it holds a
credential of its own. Run this once per machine:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\init-secrets.ps1
```

It generates one random value and writes it to two places: .NET user secrets for the
API, and `secrets\service-secret.txt` for Unreal, which cannot read user secrets.
`secrets\` is git-ignored.

**Both processes announce a fingerprint of the value they hold, and it is the fastest
thing to check when gathering returns 401.** The API logs it at startup:

```
Service credential configured from configuration (fingerprint 8e4f0b7f1d6b1fd4); environment Development.
```

and Unreal logs `Service credential loaded (44 chars, fingerprint 8e4f0b7f1d6b1fd4).`
Three cases, distinguishable at a glance:

| What the logs say | What it means |
|---|---|
| Fingerprints match, still 401 | Something else; read the refusal line, which prints both |
| Fingerprints differ | Genuinely different values — re-run `init-secrets.ps1` and restart both |
| API says `configured <none>` | The API has no secret, whatever is on disk |
| API prints no credential line at all | Stale API process from before this feature — restart it |

**The trap this exists for.** `dotnet user-secrets` are only loaded when the host is in
the **Development** environment, which depends on an environment variable being set by
whatever started the process. Launch the API a slightly different way and the secret
silently is not there, while every other endpoint keeps working — because they either
need no secret or fail somewhere more obvious. The value on disk is right, and
unreachable. That is why the API also falls back to reading
`secrets/service-secret.txt` directly, and why the startup line names both the source
and the environment.

`-GatherSelfTest` on the Unreal command line fires one gather as soon as the world
loads, with no pawn, key or range check involved. It splits "the credential path is
broken" from "the in-game path is broken" in a single run.

### A player account to play as

With the API running:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\init-player.ps1
```

It registers an account, creates a character, and writes `secrets\player-login.txt` — two lines,
email then password — which the client reads at startup. `secrets\` is git-ignored.

**Credentials go in a file, not on the command line, because command lines here mangle values.**
A launch passing `-BackendEmail=someone@gmail.com` arrived as `someone@gmail .com`, with a space
inserted before the dot; `FParse` stops at whitespace, so the client tried to log in as
`someone@gmail` and got a 401 indistinguishable from a wrong password. The same mangling turned
`-ShipStartX=39.56` into `39`. **If a numeric or dotted argument behaves as though it were
truncated, check the `LogInit: Command Line:` line in the log before suspecting anything else** —
it prints exactly what the process received.

The client logs the address it is using (never the password), so a mangled one is visible at a
glance:

```
LogSpaceMMOBackend: Signing in as player@local.test.
LogSpaceMMOBackend: Connection identified as character 10 (Prospector5752) on account 7.
```

A connection with no identity is not a fault — it simply cannot gather, and says so at the point
you try rather than failing silently.

### Querying the API from PowerShell

The QA checklists hit the API by hand. Windows PowerShell 5.1 has three traps that make
copied-from-anywhere commands fail in ways that look like server faults.

**`&&` does not exist.** Use `;` to chain unconditionally, or `A; if ($?) { B }` to chain
on success. Bash's inline `VAR=x cmd` prefix does not exist either — set `$env:VAR = 'x'`
on its own statement first. Both are why `scripts\api.bat` exists; prefer it.

**`curl` is an alias for `Invoke-WebRequest`, not curl.** So `curl -s http://…` fails on
the `-s`. Write `curl.exe` explicitly to get the real one, which Windows ships in
`System32`.

**Always assign `Invoke-RestMethod`'s result to a variable before piping or counting it.**
This is the one that actually wastes an afternoon, because it produces *wrong answers
rather than errors*:

```powershell
# WRONG — the JSON array arrives as a single object, so $_.key is an array of every
# key, `-eq` returns a non-empty array, that is truthy, and the filter matches
# everything. $cap silently becomes "2 5 4 1 3" and the next request 404s.
$cap = (Invoke-RestMethod "$base/world/bodies" | Where-Object { $_.key -eq 'body_capital' }).id

# RIGHT — assignment unrolls the array, and the filter behaves.
$bodies = Invoke-RestMethod "$base/world/bodies"
$cap = ($bodies | Where-Object { $_.key -eq 'body_capital' }).id
```

The same quirk makes an empty result count as one: `@(Invoke-RestMethod …).Count` on a
response of `[]` returns **1**, while assigning first and then counting returns **0**. A
checklist step that reads "should return no deposits" will appear to fail against a
perfectly correct endpoint.

Also note `Where-Object key -eq 'x'` — the simplified syntax without braces — silently
matches nothing here. Use the `{ $_.key -eq 'x' }` scriptblock form.

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
