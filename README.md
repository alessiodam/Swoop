# Swoop

Swoop is the CEagle app for the TI-84 Plus CE. It browses the **Roost** — the CEagle program
registry — over Wi-Fi, checks what is already on the calculator against what the Roost is
serving, and installs or updates programs straight onto it. No cable, no TI Connect.

## Building

Needs the [CE C toolchain](https://github.com/CE-Programming/toolchain).
[WiTi](https://github.com/alessiodam/WiTi) comes in as a submodule, so clone with it:

```
git clone --recursive https://github.com/alessiodam/Swoop.git
```

An existing clone catches up with `git submodule update --init`. Then build the library once and
the app against it:

```
export CEDEV=/path/to/CEdev
make -C third_party/witi/lib
make
```

`WITI` points anywhere a WiTi `lib` directory lives, so a checkout beside this one works too:
`make WITI=../WiTi/lib`.

### On CEagle Flight

Flight does not use the submodule at all. The manifest names `witi` under `[program] depends`, so
Flight fetches the archive WiTi already publishes on Roost, unpacks its stub and header, and builds
Swoop against those. The build command there is a plain `make`: the makefile notices `CEAGLE_FLIGHT`
and leaves `EXTRA_LIBLOAD_LIBS` to the environment instead of pointing it at a checkout that is not
there.

## License

Swoop is licensed under the Apache License 2.0. See [LICENSE](LICENSE) for details.
