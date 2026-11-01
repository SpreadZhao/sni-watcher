# sni-watcher

`sni-watcher` is a C11 D-Bus daemon that provides the
`org.kde.StatusNotifierWatcher` and `org.freedesktop.StatusNotifierWatcher`
interfaces on `/StatusNotifierWatcher` using `libsystemd` (`sd-bus`).

It sets `IsStatusNotifierHostRegistered = true` so tray-aware applications stay
in the background when their windows close and export their
`StatusNotifierItem` and `com.canonical.dbusmenu` objects on the user bus. When
an application disconnects from D-Bus, `sni-watcher` removes its entries and
emits `StatusNotifierItemUnregistered`.

```sh
systemctl --user status sni-watcher.service
journalctl --user -u sni-watcher.service
busctl --user get-property \
  org.kde.StatusNotifierWatcher \
  /StatusNotifierWatcher \
  org.kde.StatusNotifierWatcher \
  RegisteredStatusNotifierItems
```

## Home Manager

Add `github:SpreadZhao/sni-watcher/main` to your flake inputs and import
`inputs.sni-watcher.homeManagerModules.default`:

```nix
services.sni-watcher = {
  enable = true;
};
```

`services.sni-watcher.enable` defaults to `false`. `systemdTarget` defaults to
`config.wayland.systemd.target` (for example, `"dwl-session.target"`).

## Build

```sh
nix build
nix flake check
```

Packages, apps, checks, formatter and development shells support `x86_64-linux`
and `aarch64-linux`.
