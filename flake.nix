{
  description = "Native C New Relic MCP server";
  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-26.05";
  outputs =
    { nixpkgs, ... }:
    let
      systems = [
        "x86_64-linux"
        "aarch64-linux"
        "x86_64-darwin"
        "aarch64-darwin"
      ];
      each = f: nixpkgs.lib.genAttrs systems (system: f nixpkgs.legacyPackages.${system});
    in
    {
      packages = each (pkgs: {
        default = pkgs.callPackage ./default.nix { };
      });
      devShells = each (pkgs: {
        default = pkgs.mkShell {
          inputsFrom = [ (pkgs.callPackage ./default.nix { }) ];
        };
      });
    };
}
