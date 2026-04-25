{ pkgs ? import <nixpkgs> {} }:
pkgs.mkShell {
  packages = with pkgs; [
    clang
    llvmPackages_18.llvm
  ];
}
