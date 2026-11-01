{
  config,
  lib,
  pkgs,
  ...
}:

let
  cfg = config.services.sni-watcher;
in
{
  options.services.sni-watcher = {
    enable = lib.mkEnableOption "StatusNotifierWatcher D-Bus daemon";
    package = lib.mkOption {
      type = lib.types.package;
      default = pkgs.callPackage ./package.nix { };
      description = "sni-watcher package to run.";
    };
    systemdTarget = lib.mkOption {
      type = lib.types.str;
      default = config.wayland.systemd.target;
      description = "Systemd user target that starts and stops sni-watcher.";
    };
  };

  config = lib.mkIf cfg.enable {
    home.packages = [ cfg.package ];
    systemd.user.services.sni-watcher = {
      Unit = {
        Description = "D-Bus StatusNotifierWatcher daemon";
        After = [ "graphical-session-pre.target" ];
        PartOf = [ cfg.systemdTarget ];
      };
      Service = {
        Type = "dbus";
        BusName = "org.kde.StatusNotifierWatcher";
        ExecStart = "${cfg.package}/bin/sni-watcher";
        Restart = "on-failure";
        RestartSec = 2;
        StandardOutput = "journal";
        StandardError = "journal";
      };
      Install.WantedBy = [ cfg.systemdTarget ];
    };
  };
}
