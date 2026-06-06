# Working agreement

## Commit every significant change

Commit each significant change as you make it. Do not let uncommitted work
accrue — unstaged changes risk being lost.

- Make a separate commit per logical change, right after completing it.
- Use a single-line commit message of at most 80 characters.
- Build/verify before committing when practical.

For incremental updates to a change that has not yet been pushed to GitHub,
prefer amending the existing commit over stacking new commits, so a single unit
of work stays a single commit. Intermediate states remain recoverable via
`git reflog`. Once a commit is pushed, do not amend it — add a new commit.
