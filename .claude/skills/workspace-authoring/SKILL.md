---
name: workspace-authoring
description: Create or maintain Workspace cards, report tabs and Markdown deliverables with consistent styling, working media previews and clear project organization.
---

# Workspace authoring

Read `AGENTS.md` and [the workflow](../../../processes/workspace-authoring.md).
Keep authored content in English unless the user explicitly requests another
language. Put related iterations in an existing card rather than creating
several cards for one task. Do not reorganize unrelated projects.

Use `workspace/_shared/report.css` and `report.js` in every HTML report, with
`data-mak-report="document"` on the root element. Use the shared tokens and
layout instead of inventing another visual theme. Basic cards and media grids
can use the sample CSS in `workspace/_shared/examples.css`.

Every image must be clickable, preview at full size and offer a download.
Use local output paths, meaningful alt text and lazy loading. Keep motion
studies playable with native video controls. Wide tables must scroll within
the report. Never cover the document close button with the Files rail.

Update `workspace/workspace.json` with a stable ID, category, folder, readable
title, creation date, last updated date and steps. `sample: true` is reserved for
starter examples; ordinary work uses normal archiving behavior.

Open every changed tab at wide and narrow sizes. Check media and links, Markdown
rendering and image close/download controls. Preserve the dark surface during
loading. Report actual verification and any remaining limitations. Prepare
desktop changes without restarting active sessions until the user permits it.
