# Working on SpaceMMO

A solo, long-term hobby project. Design decisions live in `docs/adr/`; how to run anything
lives in `docs/setup.md`. This file is about how to *debug* it, because that is where the
expensive mistakes have been.

## Debugging: ask the running game, not the code

Most of this project's rules are pure functions with fast headless tests, and those catch a
lot. What they cannot catch is anything about lighting, geometry, apparent motion, scale, or
wiring — every automated check runs `-nullrhi`, which renders nothing. Those faults are found
by playing, and each round trip costs a real person a real playtest.

A terrain bug once cost seven wrong diagnoses in one evening — night side, shadow cascades,
mesh normals, render origin, construction path, runtime mesh updates — every one plausible,
every one reasoned from code that looked correct. Two things ended it, and neither was
thinking harder.

**Read the logs yourself.** `client/Saved/Logs/ClientA.log` (and `ClientB.log`) are written on
every run. They contain what was built, where it was placed, what the camera was doing, and
every console variable that was set. Grepping them beats asking what happened, and twice they
showed that an experiment reported as a result had never actually run.

**Instrument before theorising.** After the second wrong guess, stop proposing causes and
start printing numbers — positions, bounds, counts, whether a thing is visible, what a
component is actually holding. One instrumented run has repeatedly beaten five arguments.

**Find the observation that discriminates.** The question is not "what could cause this" but
"what is in this frame that rules something out". Blown-out white ore sitting on black ground,
one metre apart in the same light, killed three lighting theories at once: shadows cannot do
that, and ambient covering every direction cannot leave anything black.

**Bisect with a switch, not a rebuild.** Add a console variable and let one keypress kill a
hypothesis. `SpaceMMO.HideGlobeUnderPatch` proved which of two meshes was missing;
`SpaceMMO.RebuildGlobe` killed a theory before it could be acted on. Engine flags need no code
at all: `ShowFlag.Lighting 0` separates "unlit" from "not drawn", `ShowFlag.Wireframe 1` shows
whether geometry is there.

## Before handing over a diagnostic, ask what it looks like if it does not run

This is the rule that was learned twice in one session, both times at the user's expense.

- A variant switch was read only when the mesh rebuilt, and a wide mesh tolerates kilometres of
  drift before rebuilding. Three variants came back "did not draw" having never been built.
- A flag replaced a working mesh with a broken one and nothing rebuilt the working one, so
  turning the flag off left a permanently black sky.

Both produced output indistinguishable from a genuine negative. So: make the diagnostic log
that it ran, make changing it force whatever recomputation it affects, and make it restore what
it replaced. A measurement that can silently not happen is worse than none, because it answers
anyway.

## Tests: write the one that fails for the right reason

Green tests are not the claim. The claim is that a specific wrong behaviour would be caught.

- **Verify the test fails against the bug.** Inverting a rule, or restoring the old lookup, and
  watching the suite go red takes a minute and is the only proof a test constrains anything.
- **Feed in the real value at least once.** A market panel filtered inventory for kind `1`,
  which is `ShipHold` on the server, and five green tests missed it because every one of them
  built its inputs by hand. For anything crossing the wire, one test must use what the other
  side actually sends.
- **Do not assert literal counts of shipped content.** They fail whenever content is authored,
  which trains whoever is authoring to bump the number without reading why it moved. Count
  against the pack.

## Project facts that have each cost a session

- **Re-cook the dedicated server after code changes.** A stale staged build fails as a
  misleading `FieldCache` error. `RunUAT BuildCookRun ... -server -noclient`, ~30 minutes.
- **Re-seed after editing anything in `data/`**: `dotnet run --project services/SpaceMMO.Api --
  --seed`. Restarting the API does not do it.
- **`--seed` is also the only thing that applies migrations.** Startup deliberately does not, so a
  restart cannot rewrite a production database — but that means a new migration is unapplied until
  somebody seeds. The API now refuses to start and names the missing migrations rather than serving
  500s; before that guard existed, the symptom was a failed sign-in reading as "cannot identify my
  character", which cost a session.
- **This machine mangles Unreal command-line arguments.** Check `LogInit: Command Line:` in the
  log before believing a flag arrived. Prefer values in config files.
- **Never round-trip a source file through PowerShell** `Get-Content`/`Set-Content` — it
  corrupts UTF-8 silently. Use the editing tools.
- **The client holds a build lock while running.** Builds fail with "Unable to build while Live
  Coding is active" until it is closed.

## Reporting

Say what was verified and how. If something is unverified, say so plainly rather than letting
a green suite imply coverage it does not have. End a segment with what to look at and how it
would fail, not just that the tests pass — the manual pass is the only coverage some of this
gets.
