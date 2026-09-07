# GitHub setup

- Private repository: https://github.com/altayince/muzakere
- Private Project: https://github.com/users/altayince/projects/2 (`MUZ`)
- GLA Project copied without issues: same 13 fields and Todo/In Progress/Done status.
- Initial implementation issue: #1; branch `feature/MUZ-1-desktop-import`.
- Both issue and PR assigned to altayince and added to MUZ.
- Auto-delete merged branches enabled; main is protected for admins too.
- Pull request required, with zero mandatory approving reviews for one-person use.
- Required checks: `validate-branch`, `validate-ownership`, `windows-build-test`,
  `linux-build-test`; branch must be up to date before merging.
- Linear history and resolved conversations required; force pushes and main
  deletion blocked. Use squash or rebase merge.
- Local pre-push hook rejects main pushes and invalid MUZ branch names.
- `MUZ_PROJECT_TOKEN` installed as an encrypted Actions secret for Project checks.

GitHub accepted and returned the protection configuration during setup; no
account-plan limitation blocked it. The merge-close workflow is configured and
will be exercised by the first approved merge, not by merging a test PR.
