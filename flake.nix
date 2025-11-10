{
  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixpkgs-unstable";
    flake-utils.url = "github:numtide/flake-utils";
    arduino-nix.url = "github:bouk/arduino-nix";
    arduino-index = {
      url = "github:bouk/arduino-indexes";
      flake = false;
    };
  };

  outputs =
    {
      nixpkgs,
      flake-utils,
      arduino-nix,
      arduino-index,
      ...
    }@attrs:
    let
      overlays = [
        (arduino-nix.overlay)
        (arduino-nix.mkArduinoPackageOverlay (arduino-index + "/index/package_index.json"))
        (arduino-nix.mkArduinoLibraryOverlay (arduino-index + "/index/library_index.json"))
        (arduino-nix.mkArduinoPackageOverlay (arduino-index + "/index/package_rp2040_index.json"))
        # (arduino-nix.mkArduinoLibraryOverlay "https://espressif.github.io/arduino-esp32/package_esp32_index.json")

      ];
    in
    (flake-utils.lib.eachDefaultSystem (
      system:
      let
        pkgs = (import nixpkgs) {
          inherit system overlays;
        };
      in
      rec {
        packages.arduino-cli = pkgs.wrapArduinoCLI {
          libraries = with pkgs.arduinoLibraries; [
            # (arduino-nix.latestVersion pkgs.arduinoLibraries."nRF905 Radio Library")

            #(arduino-nix.latestVersion "https://github.com/ZakKemble/nRF905-arduino")
            # (arduino-nix.latestVersion ADS1X15)
            # (arduino-nix.latestVersion Ethernet_Generic)
            # (arduino-nix.latestVersion SCL3300)
            # (arduino-nix.latestVersion TMCStepper)
            # (arduino-nix.latestVersion pkgs.arduinoLibraries."Adafruit PWM Servo Driver Library")
          ];

          packages = with pkgs.arduinoPackages; [
            platforms.esp32.esp32."3.3.2"
            platforms.rp2040.rp2040."5.4.2"
          ];
        };
        clipath = builtins.trace "${packages.arduino-cli.dataPath}" "${packages.arduino-cli.dataPath}";
      }
    ));
}
