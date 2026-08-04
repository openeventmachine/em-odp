# Event Machine (EM) on Open Data Plane (ODP) - em-odp

For licensing information, see the LICENSE file in the top level directory.

This implementation of the Event Machine (EM) framework on top of the
Open Data Plane (ODP) is called `em-odp`. Many ODP implementations exist, both
generic and HW or SoC optimized, and all should be suitable for em-odp to
utilize and run upon.

For development, e.g., ODP for linux-generic could be most suitable, while a SoC
optimized version would provide the best performance on a specific target.

The em-odp implementation provides the EM API for the applications while ODP
acts as a portability layer as well as providing APIs and services outside the
scope of EM, e.g., ODP pkt-io or crypto APIs.

## Compilation

First clone the ODP code (here using the ODP linux-generic version):

```sh
git clone https://github.com/OpenDataPlane/odp.git
```

Go to the odp folder, create a build folder (optional) and configure,
compile and install ODP (optionally with a user specified installation folder):

```sh
cd <odp-dir>
./bootstrap
mkdir build && cd build
../configure --prefix=<odp install path>
```

If testing for performance, also give the `--disable-abi-compat` option to enable
more inlining of code. Try also `--disable-shared` if a static lib is OK for you.

```sh
../configure --prefix=<odp install path> --disable-abi-compat --disable-shared ...
```

Enabling Link Time Optimization (LTO) for EM, ODP and the application might
give further performance benefits (`--enable-lto`).

See further configure options: `../configure --help`

```sh
make && make install
```

Clone EM-ODP code:

```sh
git clone <em-odp repo>
```

Go to the em-odp folder, optionally create a build folder, then configure,
compile and install em-odp. Separate build folders are useful if you want to run
em-odp with different ODP installations or configurations.

```sh
cd em-odp
./bootstrap
mkdir build && cd build
../configure --prefix=<em-odp install path> --with-odp-path=<odp install path> \
[--with-odp-lib=libodp-linux] [--enable-esv --enable-check-level=3] ...
```

Use the options `--enable-check-level=3 --enable-esv` to help catch bugs
during development.

Try `--disable-shared` also for EM if a static lib is OK for you.

Enabling Link Time Optimization for EM, ODP and the application might give
further performance benefits (`--enable-lto`).

See further configure options: `../configure --help`

```sh
make && make install
```

Run some example application to test the functionality (path from `build/`), e.g.

```sh
./programs/example/fractal/fractal -c 0xe -t
```

Optionally change ODP and/or EM run-time options by specifying your own
runtime config files:

```sh
ODP_CONFIG_FILE=my-odp.conf EM_CONFIG_FILE=my-em.conf \
    ./programs/example/hello/hello -c 0xe -t
```

Stop by pressing Ctrl-C.

## ODP Configuration Changes for EM (em-odp)

The ODP configuration file lets the user tweak ODP settings.
For odp-linux, some settings are better modified when using EM (em-odp), read
about config file usage here: `<odp>/config/README`

Either change the default values in the config files and recompile or
override the defaults by providing your own config files at startup:

```sh
ODP_CONFIG_FILE=my-odp.conf EM_CONFIG_FILE=my-em.conf \
    ./programs/example/hello/hello -c 0xe -t
```

odp-linux default configuration file: `<odp>/config/odp-linux-generic.conf`

Usually EM works better with the following config changes:

- **Timer:** Use inline timer implementation with EM.

  ```c
  timer.inline = 0 -> 1
  ```

- **Scheduler:**

  - Priority level spread, the optimal value is typically the number of threads
    using the scheduler:

    ```c
    sched_basic.prio_spread = 4 -> 'number of EM cores used'
    ```

  - Disable ODP automatically updated schedule groups, EM does not need them.
    (These options are actually DEPRECATED so prefer using the ODP API function
    `odp_schedule_config()` instead)

    ```c
    sched_basic.group_enable: {all     = 1 -> 0
                               worker  = 1 -> 0
                               control = 1 -> 0}
    ```

  - Ordered queue reorder stash size. ODP might drop events if the
    stash/queue becomes full, causing EM ESV failure since event(s) dropped
    outside of EM can't be tracked — thus disable this.

    ```c
    sched_basic.order_stash_size = 512 -> 0
    ```

## CPU Architecture Specific Options

Arch specific compilation options should be passed to EM and ODP via the
configure script.

Example for a made-up ARMv8 SoC called "soc-x":

```sh
# ODP configuration for soc-x
odp-soc-x/build> ../configure ... --with-platform=soc-x [default=linux-generic] ...

# EM-ODP configuration for soc-x
em-odp/build> ../configure ... CFLAGS="-O3 -march=armv8.2-a+... -mcpu=soc-x" \
    --with-odp-path=<odp-soc-x install path> \
    --with-odp-lib=libodp-soc-x ...
```

The whole chain of used SW libs/dependencies should preferably be compiled
with the same `-march`/`-mcpu` options for best results:

```sh
other-libs/build> ../configure ... CFLAGS="-O3 -march=armv8.2-a+... -mcpu=soc-x"
```

## Debug Compilation

For debugging activities it may be useful to compile separate debug libs and
executables of ODP and EM-ODP. The configuration script has to be run with
different options, other commands as above.

EM-ODP debug configuration supports several debug-print-sets, which are:

| Option                       | Description                                  |
| ---------------------------- | -------------------------------------------- |
| `--enable-debug-print-all`   | Set all debug prints on or off               |
| `--enable-debug-print-base`  | Set basic set of debug prints on or off      |
| `--enable-debug-print-timer` | Set timer debug prints on or off             |

The specific ones overwrite the `--enable-debug-print-all` setting.

For backward compatibility reasons `--enable-debug-print` is an alias for
`--enable-debug-print-all`.

**ODP debug configuration:**

```sh
../configure --prefix=<odp debug install path> \
    --enable-debug --enable-helper-debug CFLAGS='-O0 -g3' ...
```

**EM-ODP debug configuration:**

```sh
../configure --prefix=<em-odp debug install path> \
    --with-odp-path=<odp install path> \
    --enable-check-level=3 --enable-esv --enable-debug-print-all \
    --disable-debug-print-timer CFLAGS='-O0 -g3' ...
```

## Packet-I/O (ODP linux-generic + socket or DPDK based I/O)

The default ODP for linux-generic has pkt-io support based on linux networking
and sockets that is quite slow but works nicely for development purposes.

The linux-generic version of ODP also supports DPDK based packet I/O. Use DPDK
based packet I/O on x86 targets for better performance.

Alternatively, try XDP sockets (AF_XDP) that have been added to odp-linux.
See odp-linux documentation for setup.

### DPDK I/O

DPDK instructions can be found at <https://doc.dpdk.org/guides/>.
DPDK version 24.11 has been used in testing.

DPDK can be used to accelerate packet I/O on x86 targets with the linux-generic
version of ODP. This is different from the separate odp-dpdk implementation
described below, but requires the same DPDK installation.

DPDK can be installed via e.g. `apt-get` or `yum` or compiled from source code from
<https://www.dpdk.org/>.
See the supported DPDK versions from the ODP documentation.

See ODP and DPDK instructions for detailed compilation info.
Remember to configure selected network interfaces to use DPDK drivers, see the
DPDK docs.

After DPDK installation, configure and compile odp-linux-generic.

**Compile odp-linux-generic to use dpdk-pktio:**

```sh
cd odp/build/  # assume odp/bootstrap already run
../configure --prefix=<odp install path> \
    --enable-dpdk [--enable-dpdk-zero-copy] \
    PKG_CONFIG_PATH=<dpdk install path>/lib/x86_64-linux-gnu/pkgconfig: ...
make -j16 install
```

**Compile em-odp to use the installed odp-linux with dpdk-pktio:**

```sh
cd em-odp/build  # assume em-odp/bootstrap already run
../configure --prefix=<em install path> --with-odp-path=<odp install path> \
    PKG_CONFIG_PATH=<dpdk install path>/lib/x86_64-linux-gnu/pkgconfig: ...
make -j16 install
```

Make sure the `--with-odp-path` points to the ODP with dpdk-pktio installed above,
then, e.g., start an example packet I/O application.

> **Note:** DPDK interfaces are accessed using only indexes and not "ethX" names.

```sh
cd em-odp/build
sudo ./programs/packet_io/loopback -c 0xe -t -i 0,1
```

## Using odp-dpdk (note: different from ODP linux-generic + DPDK pkt-io)

There is also an ODP version that is optimized to run on top of Intel DPDK
— this version is called odp-dpdk.

Note that also ODP for linux-generic can use DPDK for packet-io, but the
optimized odp-dpdk utilizes DPDK internally for other functionality also.

DPDK instructions can be found at <https://doc.dpdk.org/guides/>.

Get the odp-dpdk code:

```sh
git clone https://github.com/OpenDataPlane/odp-dpdk.git
```

The odp-dpdk readme file (`<odp-dpdk>/platform/linux-dpdk/README`) describes how
to configure and compile DPDK and odp-dpdk on top of it.

ODP compilation as above but with different installation directories, i.e.:

```sh
../configure --prefix=<odp-dpdk install path> [--enable-dpdk-zero-copy] ... \
    PKG_CONFIG_PATH=<dpdk install path>/lib/x86_64-linux-gnu/pkgconfig: ...
```

**EM-ODP compilation with odp-dpdk:**

```sh
cd em-odp
mkdir build--em-odp-dpdk && cd build--odp-dpdk
../configure --prefix=<em install path> \
    --with-odp-path=<odp-dpdk install path> \
    --with-odp-lib=libodp-dpdk ...
```
