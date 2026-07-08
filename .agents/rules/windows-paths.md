# Windows Path Separators & Sandbox Permissions

This rule ensures that the agent uses correct Windows path separators when invoking tools to prevent unauthorized path warnings or permission prompt requests in the Antigravity sandbox.

## Context
On Windows, the workspace is configured as `D:\esp32-nut`.
If the agent makes tool calls (like reading, writing, or listing files) using forward slashes (`/`), the sandbox matches the path against `/` (which triggers the `write_file(/): ask` rule) instead of matching it to the workspace directory.

## Rules
1. **Always use backslashes (`\`)** as the path separator in absolute file paths for all tool executions (e.g., `view_file`, `write_to_file`, `replace_file_content`, `list_dir`, `run_command`).
   - *Correct:* `D:\esp32-nut\src\main.cpp`
   - *Incorrect:* `D:/esp32-nut/src/main.cpp`
2. **Clickable Links Exception:** In markdown responses intended for the user, continue using forward slashes inside file URIs so that they are correctly interpreted as clickable links by the IDE/VS Code (e.g., `[main.cpp](file:///D:/esp32-nut/src/main.cpp)`).
