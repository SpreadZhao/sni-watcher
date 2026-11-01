{
  description = "D-Bus StatusNotifierWatcher daemon";

  inputs = {
    nixpkgs.url = "github:nixos/nixpkgs/nixos-unstable";
  };

  outputs =
    { self, nixpkgs, ... }:
    let
      systems = [
        "x86_64-linux"
        "aarch64-linux"
      ];
      eachSystem = nixpkgs.lib.genAttrs systems;
      mkPkgs = system: import nixpkgs { inherit system; };
    in
    {
      packages = eachSystem (
        system:
        let
          pkgs = mkPkgs system;
          package = pkgs.callPackage ./nix/package.nix { };
        in
        {
          default = package;
          sni-watcher = package;
        }
      );

      apps = eachSystem (system: {
        default = {
          type = "app";
          program = "${self.packages.${system}.default}/bin/sni-watcher";
          meta.description = self.packages.${system}.default.meta.description;
        };
        sni-watcher = self.apps.${system}.default;
      });

      homeManagerModules.default = import ./nix/home-manager.nix;

      formatter = eachSystem (system: (mkPkgs system).nixfmt);

      devShells = eachSystem (
        system:
        let
          pkgs = mkPkgs system;
        in
        {
          default = pkgs.mkShell {
            packages = with pkgs; [
              clang-tools
              gcc
              nixfmt
              pkg-config
              systemd
            ];
          };
        }
      );

      checks = eachSystem (system: {
        default = self.packages.${system}.default;
      });
    };
}
