# check-link-consistency for ArchLinux

It does as it sounds, and much better than [`findbrokenpkgs`](https://aur.archlinux.org/packages/findbrokenpkgs/) (AUR port of Gentoo's `revdep-rebuild` script):

* Analyzes optional dependencies (downloads but **NOT** installs) to exclude them from reported errors.

* Has [config  file](src/etc/check-link-consistency.conf.sample) where you can add more directories to scan for bins & libs, add library search path for packages or files. Together with optdeps analysis it makes "your system is consistent" outcome reachable.

* Really fast. Without optdeps analysis (i.e. with `-O` option, pretty useless mode) on my system with warm disk cache it takes 0.35s against 3m10s for `findbrokenpkgs`.

## Install

1. On [ArtixLinux](https://artixlinux.org/), install package `check-link-consistency` from `universe` repository. Otherwise, clone or download sources, then run `make && sudo make install`. Binary will go to `/usr/bin/`, sample config -- to `/usr/share/check-link-consistency/`.

2. Copy `/usr/share/check-link-consistency/check-link-consistency.conf.sample` to `/etc/check-link-consistency.conf` and edit.

## Run

Run as `root`. It does not perform any system modifications, but calls `pacman -Sw` to download optional dependencies.

To see warnings and `pacman -Sw` output, run with `-v` option; it's **useful to investigate problems**. Try `-h` for more options.

**ATTENTION:** First run downloads **LOTS** of packages. From now on, **you don't want** to run `paccache -dvuk0` because I'll re-download everything again; but you can safely run `paccache -dvuk1`.

## Motivation

Binary distros need such tool much more than source-based. From user's point of view it's like static vs dynamic typing in programming languages: source-based distro checks package's dependencies when program builds, binary -- when it runs. Funny that [I've hit this problem](https://forum.artixlinux.org/index.php/topic,3331.msg21592.html#msg21592) in less than a week after switching from Gentoo to Artix.

## Want symbol resolution? No you don't.

For 6 months I used version that performs not only `.so` resolution, but also symbol resolution. Turned out 0% use + 100% trouble: 

* Never seen anything but false positives.

* Config file is 5 times larger and requires editing on every upgrade of core packages like gcc, perl, python, nvidia drivers, etc.: lots of `.so` dependencies had to be specified manually and their paths contain versions.

* Correct LOCAL symbol resolution is still a [mystery](https://stackoverflow.com/questions/70920442/local-symbols-in-elfs-dynsym-section-are-not-actually-local-how-to-tell).

But if you're interested, I can upload this codebase into separate `syms` branch.
