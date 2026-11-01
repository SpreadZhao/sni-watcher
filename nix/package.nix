{
  lib,
  stdenv,
  pkg-config,
  systemd,
}:

stdenv.mkDerivation {
  pname = "sni-watcher";
  version = "0.1.0";
  src = ../src;

  nativeBuildInputs = [ pkg-config ];
  buildInputs = [ systemd ];

  buildPhase = ''
    runHook preBuild
    $CC -std=c11 -O2 -Wall -Wextra -Werror -pedantic \
      $(pkg-config --cflags libsystemd) \
      -o sni-watcher sni-watcher.c \
      $(pkg-config --libs libsystemd)
    runHook postBuild
  '';

  installPhase = ''
    runHook preInstall
    install -Dm755 sni-watcher "$out/bin/sni-watcher"
    runHook postInstall
  '';

  meta = {
    description = "D-Bus StatusNotifierWatcher daemon";
    license = lib.licenses.mit;
    mainProgram = "sni-watcher";
    platforms = lib.platforms.linux;
  };
}
