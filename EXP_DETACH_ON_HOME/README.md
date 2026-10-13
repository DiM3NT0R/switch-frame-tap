# Detach on HOME (default on)

Ends the live capture session when the game stops presenting (HOME / Album),
frees the donor heap immediately, and holds engine re-setup for a few seconds
so home menu / LibraryApplet init is not racing a 42 MB remount. A fresh
session starts when the title presents again.

## Default

**On.** To keep the old "still see the suspended game on HOME" behaviour:

```ini
detach_on_home = 0
```

in `sdmc:/config/switch-frame-tap/config.ini`.

## Viewer effect

On HOME the viewer shows "waiting for a game" instead of the frozen/suspended
game picture. Returning to the game reconnects on its own.

## Log lines

`sdmc:/config/switch-frame-tap/log.txt`:

- `detach-on-home=on` (or `off`)
- `home: detach-on-home - release capture memory for home menu (no engine re-setup for 5 s)`
- `HOME / game stopped presenting` / session ended
- next `live[N]: attached` when the game presents again
