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
| .NET Framework SDK 4.8 | ✅ installed — editor builds, 18 automation tests pass |
| UE source build | ❌ **required for any dedicated server target** — see below |

**Everything M1 needs is installed**, and both the UE game and editor targets compile with the
18 automation tests passing. One thing still blocks part of M2: dedicated server targets
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
