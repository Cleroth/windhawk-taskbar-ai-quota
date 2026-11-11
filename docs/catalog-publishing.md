# Publishing to the windhawk-mods catalog

Catalog updates go by PR to `ramensoftware/windhawk-mods`, authored as `Cleroth` through `gh`.

## Catalog rules (upstream README)

- The PR changes **exactly one file**: `mods/taskbar-ai-quota.wh.cpp`, a copy of the generated
  file without the local-dev `local@` filename prefix. `@id` (`taskbar-ai-quota`) never has it.
- `@github` is required and must be the PR author's profile URL, `https://github.com/Cleroth`;
  only that author can update the mod.
- No `@license` means MIT; we set `@license MIT` explicitly. Metadata lives in
  `src/taskbar-ai-quota.cpp`.
- Every update bumps `@version`.
- The catalog readme hot-links `screenshots/*.png` from this repo's `master`: push `master` before
  the PR's checks run. The published version's readme links the same way: keep a replaced
  screenshot's old file until the update that drops its link merges, then delete it.

## PR title, body and changelog

Catalog PRs are squash-merged: the squash commit and public changelog use the **PR title and
body**, not the branch's commit messages. There is no `@changelog` field or in-file changelog.

- Title: `Update taskbar-ai-quota: <summary> (vX.Y.Z)`.
- Body: upstream's `.github/pull_request_template.md`, kept intact, with the changelog replacing
  the sample items between its single `<!-- changelog:start -->` and `<!-- changelog:end -->`
  markers; upstream's PR validation fails an update without one. It covers every user-visible
  change since the catalog version.
- Changelog: stable and user-facing. Short, plain hyphen bullets starting with Add, Fix, or
  Improve; no headings, implementation details, API-field explanations, or long parentheticals.
  Example: `- Fix quota bars not appearing on newer Windows 11 taskbars.` Add a brief `Note:`
  after the list only when users need migration or compatibility information.
- A large update may group its bullets under bold category labels (`**Taskbar**`), never Markdown
  headings: the catalog's changelog page puts each version under its own `##` heading. Small
  updates stay one flat list.
- AI-review responses, design rationale and minor-fix details go in PR comments.

## Workflow

Prerequisites: `git`, `gh` authed as Cleroth, Python with PyYAML.

`pwsh catalog-update.ps1` (Workbench: Prepare Catalog Update) runs `check-mod.ps1 -Link` (so a
mod Windhawk can't build fails here, not on the PR), syncs a
persistent clone of the `Cleroth/windhawk-mods` fork with upstream `main`, checks out
`taskbar-ai-quota-<version>` (`-Branch` overrides), copies the mod, and verifies it is
byte-identical, newer than the catalog version, and the only changed file. It then runs upstream's
PR validation on it (the clone's `.github/pr_validation.py`, as gh's user) and fails on any
warning; `generate-mod.ps1 -Locate` maps its line numbers. The PR description's checks run only on
GitHub. It never commits, pushes, or edits PR text; `-Verify` rechecks the worktree without
changing it. Then:

```pwsh
$wt = "$env:LOCALAPPDATA\windhawk-taskbar-ai-quota\windhawk-mods"
git -C $wt add mods/taskbar-ai-quota.wh.cpp   # stage ONLY this file
git -C $wt commit -m "<msg>"
git -C $wt push -u origin <branch>
gh pr create -R ramensoftware/windhawk-mods -B main -H Cleroth:<branch>
```

## Review and readiness

`pwsh pr-review-status.ps1` (`-PrNumber` optional; Workbench: PR Review Status) reports checks and
whether the latest AI review matches the PR head.

- AI reviews are bound to the exact PR head SHA; any push invalidates readiness.
- The reviewer bot currently allows three reviews per 24 hours: request `/ai-review` only after the
  branch settles.
- Answer incorrect findings in comments instead of changing working code.
- Use `/ready-for-reviewer` only after checks pass and the current head has been reviewed.
