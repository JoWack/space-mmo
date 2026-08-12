# Working on SpaceMMO

A solo, long-term hobby project. Design decisions live in `docs/adr/`; planned work lives in
`docs/tasks.md`; how to run anything lives in `docs/setup.md`. This file is about how to *debug*
it, because that is where the expensive mistakes have been.

## Planned work goes in `docs/tasks.md`, not in a task list

Read `docs/tasks.md` at the start of any session that continues existing work, and cite tasks by
number when reporting.

The assistant's in-session task list does not survive a cleared context. A terrain investigation
spanning eight commits and several playtests was once resumed with "there should be tasks
recorded, I believe the terrain work is #84" — and the list was empty, with nothing to recover.
Everything worth remembering next week belongs in the repository, where git keeps it and a fresh
session reads it for free. Use the in-session list for the current sitting if it helps; it is
scratch.

- **Identifiers are permanent and never reused.** They are how a task is referred to in
  conversation months later.
- **Record what has been ruled out, and how.** A task that only says what to do invites
  re-deriving the eliminations at playtest prices. This is the same reason the debugging rules
  below exist, and it is most of what makes the file worth keeping.
- **Say what a task is blocked on**, by number, so the order of work is visible without
  reconstructing it.
- **Never present a reconstruction as a recovered original.** If a task was rebuilt from commits
  and code, label it so, and leave a gap where a number's content is genuinely lost. An invented
  history is worse than an admitted one.
- **Update it in the same commit as the work**, so a task's state and the code agree.

## Milestones live in `README.md`, and must match what the design documents assume

Two different things go missing, and only one of them is about context.

A cleared context loses the **assistant's** task list, which is what `docs/tasks.md` above exists to
prevent. But the combat milestone was never lost at all — it had simply never been written down.
`docs/design-bible.md` §2 defines eight combat and pilot skills and defers `constitution` and
`stamina` XP "to the combat milestone", and ADRs 0006, 0008 and 0009 settle who may shoot whom, what
a security zone means and what dying costs. Three accepted decisions and a skill tree, all
describing a milestone the roadmap did not list, for months, with nothing to shoot with.

So the roadmap is not just a plan — it is a **reconciliation**, and it has to name every milestone
the design documents assume.

- **When a design document says "deferred to the X milestone", X belongs in the roadmap**, that day,
  even if nothing about it is scheduled. A milestone nobody has written down cannot be prioritised,
  deferred deliberately, or noticed as missing.
- **When an ADR is accepted, check the roadmap can host it.** ADR-0006's death rules had no
  milestone to live in and quietly became inert; ADR-0007 deleted M4's entire content and the line
  went unedited for months, promising a galaxy that had been cancelled.
- **Never tell anyone a cleared context loses nothing.** It loses everything not written down. The
  honest version is that anything in the repository survives and anything else does not, which is
  why writing it down is the whole discipline rather than a courtesy.

## Show interface work before building it

**Present every widget, screen, layout and HUD arrangement to Joe and get a yes before implementing
it.** A sketch, a mock, an ASCII layout, a described arrangement — whatever carries the idea. This
covers changes as much as new screens: moving a row, re-ordering a panel, or deciding what a line
says is the same decision at a smaller scale.

Everything else here can be checked against a test, a log, or engine source. Interface cannot.
A panel can satisfy every requirement anyone wrote down and still be the wrong shape to look at, and
that only surfaces once it exists — at which point it is a day of work to argue about rather than a
sketch to redraw. Asking first costs a message.

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
