# Wedge/Unwedge for RACE

This repo provides scripts to custom-build the
wedge/unwedge JEL steganography libraries for RACE.

## License

The jel2 library is licensed under the 3-Clause BSD license.

Only the build scripts in this top-level directory are licensed under Apache 2.0.

## Dependencies

## How To Build

The [ext-builder](https://github.com/tst-race/ext-builder) image is used to
build libzip.

```
git clone https://github.com/tst-race/ext-builder.git
git clone https://github.com/tst-race/ext-jel2.git
./ext-builder/build.py \
    --target linux-x86_64 \
    ./ext-jel2
```

## Platforms

jel2 is built for the following platforms:

* `linux-x86_64`
* `linux-arm64-v8a`
* `android-x86_64`
* `android-arm64-v8a`

## How It Is Used

jel2 is used by the Destini plugins, but must be built into the Race APK.
