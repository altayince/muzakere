# MUZ Workflow

Work starts with an issue assigned to `altayince` and added to the private **MUZ**
Project. Branch from updated `main` using `feature/MUZ-<issue>-description` or
`bugfix/MUZ-<issue>-description`. Develop and test on that branch, open a PR,
assign it to `altayince`, and add the PR to MUZ too. Review and merge only after
checks pass. Never push feature work directly to main. The initial empty-repo
bootstrap is the sole exception.

The GLA controls are retained: `validate-branch` verifies naming and issue
existence; `validate-ownership` verifies assignee and Project membership; a merge
closes the branch's issue, while closing an unmerged PR leaves it open. Windows
and Linux build/test checks also run. No required second-person approval for this
single-user project. The GUI's **Neler değişti?** tab contains issue-numbered change
notes, newest first, at most 10 entries.

Install local push protection after every clone:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/install-githooks.ps1
```

The Project check uses the repository secret `MUZ_PROJECT_TOKEN`, because the
default Actions token cannot read private user Projects. It needs repository and
Project read access; rotate it when the authorized GitHub credential changes.
Never print or commit its value. Only trusted workflow code may use this secret;
the ownership job checks metadata and does not check out PR code.

See `docs/github-setup.md` for the actual remote protection configuration and any
GitHub account limitations. Local hooks alone are not server-side enforcement.
