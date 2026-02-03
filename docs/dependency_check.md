# Dependency Check

This repo uses a simple static check to detect dependency direction violations
based on `#include` statements.

Run from the repo root:

```powershell
tools/check_deps.ps1
```

What it checks:
1. `config` and `utils` must not include `core/`, `ai/`, `ui/`, `behavior/`, or
   `audio/`.
2. `ui` must not include `core/`, `behavior/`, or `audio/`. (`ai/` is allowed for
   DTO/view-model state only.)
3. `ai` must not include `core/` or `behavior/`. `ui/ui_types.h` is allowed.
4. `behavior` must not include `core/`, `ui/`, or `audio/`. (`ai/` is allowed
   for high-level state only.)
