# Name the User Config Directory after Vela, not VSR

Per-user state moves from `~/.config/vsr/` to `~/.config/vela/`, or
`$XDG_CONFIG_HOME/vela/` when that variable is set on Linux, and
`%APPDATA%\vela\` on Windows. One `vsr_core` helper resolves the directory
for every library and application. The rename follows the line between
**Vela**, the product a user installs, and **VSR**, the libraries and
formats it is built on. Per-user state belongs to the product. The `.vsr`
extension, the `share/vsr/` install tree and the `VSR_*` environment
variables name the library and stay as they are.

Existing `~/.config/vsr/` contents are not migrated automatically and are not
read as a fallback. When the old directory exists and the new one does not,
Vela logs one warning at startup that names both paths. An automatic move was
rejected because it touches user-authored color maps and Lua scripts
without asking. A permanent dual-path read was rejected because it would
never be removed. The Application Preferences file is renamed from
`appSettings.vsr` to `preferences.vsr` at the same time, because the break
is already happening.
