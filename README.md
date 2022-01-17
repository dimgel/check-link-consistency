# check-link-consistency for ArchLinux

It does as it sounds, and much better than [`findbrokenpkgs`](https://aur.archlinux.org/packages/findbrokenpkgs/) (AUR port of Gentoo's `revdep-rebuild` script):

* Analyzes optional dependencies (downloads but **NOT** installs) to exclude them from reported errors.

* Has config file. There you can add more directories to scan for bins & libs, add library search path for packages or files. Together with optdeps analysis it makes "your system is consistent" outcome reachable.

* Really fast. Without optdeps analisys (i.e. with `-O` option, pretty useless mode) on my system with warm disk cache it takes 0.35s against 3m10s for `findbrokenpkgs`.

## Install

1. Clone or download sources, run `make && sudo make install`. Binary will go to `/usr/local/bin`, sample config -- to `/usr/local/etc`.

2. Copy/rename `check-link-consistency.conf.sample` to `check-link-consistency.conf` and edit.

**NOTE:** Config file is first searched next to binary, then in `/usr/local/etc`. `LD_LIBRARY_PATH` is also taken into account.

## Run

Run as `root`. It does not perform any system modifications, but calls `pacman -Sw` to download optional dependencies.

To see warnings and `pacman -Sw` output, run with `-v` option; it's **useful to investigate problems**. Try `-h` for more options.

**ATTENTION:** First run downloads **LOTS** of packages. From now on, **you don't want** to run `paccache -dvuk0` because I'll re-download everything again; but you can safely run `paccache -dvuk1`.

## Motivation

Binary distros need such tool much more than source-based. It's like static vs dynamic typing in programming languages: source-based distro checks package's dependencies when program builds, binary -- when it runs. Funny that I've hit this problem in less than a week after switching from Gentoo to **Artix**.
