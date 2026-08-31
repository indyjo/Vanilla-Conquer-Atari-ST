# Hatari CPU profile (playback)

Debugger scripts for Hatari: `profile on` at `HatariProfileStart` (`StPlaybackTiming_Start`) and save at `HatariProfileEnd`.

`:file` paths are relative to this directory (Hatari `chdir`s here). `symbols prg` still loads `cnc.sym` next to the TTP on the GEMDOS HD. Generate that with `tiberiandawn/dump_symbols.sh` (**mangled** names; do not `--demangle`).

```text
hatari --parse tiberiandawn/tools/hatari/profile-pexec.ini …
```

Do not put `:noinit` on the end breakpoint or `profile save` writes callers only (`Disassembled 0`). After save, `c++filt` labels in the disassembly; do not `c++filt` the callee graph in place (commas break the format). `profile.txt` is gitignored.
