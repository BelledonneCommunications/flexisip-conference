# Flexisip-conference

Flexisip is a comprehensive, modular and scalable SIP server suite written in C++20. This repository contains the conference server of the suite.
For other services, see [Flexisip](https://giblab.linphone.org/BC/public/flexisip).

The conference server enables group voice and video calls as well as instant messaging in group chats.

## Deployment and Applications:

* **Server-based VoIP Service**: Flexisip can be deployed on server machines to run a full-fledged SIP VoIP service.
  This is exemplified by the free linphone.org service, which has been powered by Flexisip since 2011. Users can create
  SIP accounts on this service to connect with each other.
* **Embedded Solutions**: Flexisip can also be embedded and run seamlessly on smaller hardware systems, making it
  suitable for various embedded applications.

# License

Copyright © Belledonne Communications

Flexisip is dual licensed, and can be licensed and distributed:

- under a GNU Affero GPLv3 license for free (see COPYING file for details)
- under a proprietary license, for closed source projects. Contact Belledonne Communications for any question about
  [costs and services](https://www.linphone.org/en/flexisip-sip-server/#flexisip-license).

# Documentation

- [Supported features and RFCs](https://www.linphone.org/en/flexisip-sip-server/#flexisip-software)
- [Flexisip documentation](https://www.linphone.org/en/flexisip-sip-server/#flexisip-documentation)

# Dependencies

| Dependency      | Description                                                                                                     | Mandatory | Enabled by default |
|:----------------|:----------------------------------------------------------------------------------------------------------------|:---------:|:------------------:|
| OpenSSL         | TLS stack.                                                                                                      |     X     |                    |
| LibNgHttp2      | HTTP2 stack.                                                                                                    |     X     |                    |
| libsrtp2        | Secure RTP (SRTP) and UST Reference Implementations                                                             |     X     |                    |
| SQLite3         | Library for handling SQlite3 file                                                                               |     X     |                    |
| libmysql-client | Client library for MySQL database.                                                                              |     X     |                    |
| Hiredis         | Redis DB client library, used for Registrar DB and communications between Flexisip instances of a same cluster. |           |         X          |
| NetSNMP         | SNMP library, used for SNMP support. (-DENABME\_SNMP=YES)                                                       |           |         X          |
| XercesC         | XML parser.                                                                                                     |           |         X          |
| jsoncpp         | JSON parsing and writing.                                                                                       |           |         X          |
| libsystemd      | Library to interact with SystemD                                                                                |           |         X          |

# Compilation

## Required build tools

- C and C++ compiler. GCC (>= 13.0) and Clang (>= 19.0) are supported *they must be recent enough to support C++20 code*.
  - Debian 12: Only Clang is supported as gcc is too old for C++20.
- make or Ninja
- Python >= 3
- Doxygen
- Git

## Building Flexisip with CMake

Create a build directory and configure the project:

### From cloned GIT repository

```bash
mkdir ./build
cmake -S . -B ./build
cmake --build ./build -j<njobs>
```

### Custom
When built outside a git repository, you have to manually mention Flexisip and Linphone-SDK versions.

```bash
mkdir ./build
cmake -S . -B ./build -DFLEXISIP_CONFERENCE_VERSION=<version> -DLINPHONESDK_VERSION=<version>
cmake --build ./build -j<njobs>
```

### Some tips

Check *CMakeLists.txt* to know the list of the available options and their default value. To change an option, invoke
*CMake* again and specify the option you need to change.
For instance, here is how to disable the build of unit tests:

```bash
cmake ./build -DENABLE_UNIT_TESTS=OFF
cmake --build ./build -j<njobs>
```

You may also use *ccmake* or *cmake-gui* utilities to interactively configure the project:

```bash
ccmake ./build
cmake --build ./build -j<njobs>
```

## Building RPM or DEB packages

This procedure will help you generate a unique RPM package containing Flexisip, all its dependencies and the
corresponding package for debug symbols.
The following options are relevant for packaging:

| Option                         | Description                                                                                                                                     |
|:-------------------------------|:------------------------------------------------------------------------------------------------------------------------------------------------|
| `CMAKE_INSTALL_PREFIX`         | Prefix path where the package will install the files.                                                                                           |
| `SYSCONF_INSTALL_DIR`          | Directory where Flexisip expects to find its default configuration. If not specified, it follows LFH rules depending on `CMAKE_INSTALL_PREFIX`. |
| `FLEXISIP_SYSTEMD_INSTALL_DIR` | Directory where systemd units will be installed.                                                                                                |
| `LOGROTATE_DIR`                | Directory where the logrotate file will be installed.                                                                                           |
| `CMAKE_BUILD_TYPE`             | Set it to “RelWithDebInfo” to have debug symbols in the debuginfo package.                                                                      |
| `CPACK_GENERATOR`              | Package type: “RPM” or “DEB”.                                                                                                                   |

```bash
cmake ./build -DCMAKE_INSTALL_PREFIX=/opt/belledonne-communications/flexisip-conference -DCMAKE_BUILD_TYPE=RelWithDebInfo -DFLEXISIP_SYSTEMD_INSTALL_DIR=/usr/lib/systemd/system -DLOGROTATE_DIR=/etc/logrotate.d -DCPACK_GENERATOR=RPM
cmake --build ./build -j<njobs> package
```

Packages are now available in the `./build` directory.

[More info on RPM packaging](./packaging/rpm/README.md)

## Docker

A docker image can be built from sources using the following command:

```bash
docker build -t flexisip --build-arg='njobs=<njobs>' -f docker/flex-from-src .
```

## Nix ❄️

Flexisip can also be compiled with [Nix]. From the root of the repository, you can obtain a development shell using:

```sh
nix-shell
```

Or with Flakes enabled:

```sh
nix develop
```

Nix makes it easier to have a reproducible development environment on any Linux distribution, and doesn't interfere with
other installed tooling. It is just an additional, **optional** way to build flexisip.

### Example build commands:

```sh
CC=gcc CXX=g++ BUILD_DIR_NAME="build" cmake -DCMAKE_EXPORT_COMPILE_COMMANDS=1 -S . -B ./$BUILD_DIR_NAME -G "Ninja" -DCMAKE_BUILD_TYPE=Debug -DCMAKE_INSTALL_PREFIX="$PWD/$BUILD_DIR_NAME/install" -DENABLE_UNIT_TESTS=ON -DENABLE_STRICT_LINPHONESDK=OFF -DINTERNAL_JSONCPP=OFF
cd build
clear && cmake --build . --target install && LSAN_OPTIONS="suppressions=../sanitizer_ignore.txt" bin/flexisip_conference_tester --verbose
```

### Note to maintainers

At the exception to [`shell.nix`](./shell.nix), `.nix` files should live inside the [`nix/`](./nix/) folder.

All `.nix` files should be formatted with `nixpkgs-fmt`.

[Nix]: https://nixos.org/

# Configuration

Flexisip needs a configuration file to run correctly.
Use `./flexisip-conference --dump-all-default > flexisip-conference.conf` to generate a documented default configuration
file.