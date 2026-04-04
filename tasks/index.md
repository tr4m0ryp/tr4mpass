# Itr4m -- Task Manifest

## Execution Summary
- Total tasks: 14
- Parallel groups: 6
- Estimated agents: 14 (+ 1 de-sloppify)

## Dependency Graph

| Task | Title                      | Depends On | Model  | Isolation | Group |
|------|----------------------------|-----------|--------|-----------|-------|
| 001  | Project scaffolding        | none      | sonnet | worktree  | 1     |
| 002  | Utility modules            | none      | sonnet | worktree  | 1     |
| 003  | Chip database              | none      | sonnet | worktree  | 1     |
| 004  | Device module (extended)   | 001       | opus   | worktree  | 2     |
| 005  | USB DFU module             | 001, 002  | opus   | worktree  | 2     |
| 006  | Bypass registry            | 001       | sonnet | worktree  | 2     |
| 007  | DFU protocol               | 005       | opus   | worktree  | 3     |
| 008  | Signal detection           | 004       | sonnet | worktree  | 3     |
| 009  | Activation client          | 004       | opus   | worktree  | 3     |
| 010  | checkm8 exploit            | 003, 007  | opus   | worktree  | 4     |
| 011  | Session + record + delete  | 009       | opus   | worktree  | 4     |
| 012  | Path A (A5-A11)            | 006, 010  | opus   | worktree  | 5     |
| 013  | Path B (A12+)              | 006, 011  | opus   | worktree  | 5     |
| 014  | Main + start.sh            | 012, 013  | opus   | worktree  | 6     |

## Execution Order
- **Group 1** (parallel, 3 agents): 001, 002, 003 -- scaffolding, utils, chip DB
- **Group 2** (parallel, 3 agents): 004, 005, 006 -- device, USB DFU, bypass registry
- **Group 3** (parallel, 3 agents): 007, 008, 009 -- DFU proto, signal, activation
- **Group 4** (parallel, 2 agents): 010, 011 -- checkm8 exploit, session/record/delete
- **Group 5** (parallel, 2 agents): 012, 013 -- Path A, Path B
- **Group 6** (1 agent): 014 -- main.c orchestration + start.sh
- **Final**: de-sloppify pass + integration build
