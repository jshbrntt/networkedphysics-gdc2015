
# Networking for Physics Programmers GDC 2015 Example Source Code

This is a fork of Glenn Fiedler's original project found [here](https://github.com/gafferongames/networkedphysics-gdc2015).

I was unable to compile the `release` branch of the original project on **macOS Monterey - Version 12.3.1**.

Which is a shame as the [original presentation](https://gdcvault.com/play/1022195/Physics-for-Game-Programmers-Networking) and the demo code is an incredible resource for anyone who wants to understand the different approaches to tackling netcode for physics simulations.

So I forked it in order to make the necessary changes which would enable it to compile (on my machine).

I also did not find it helpful that the specific versions of software that the original project used were not documented so I will provide the ones I used here.

## 1. Building the source code

The environment I used to build the source is as follows.

```shell
$ system_profiler SPSoftwareDataType
Software:

    System Software Overview:

      System Version: macOS 12.3.1 (21E258)
      Kernel Version: Darwin 21.4.0
      # ...
```

### 1.1 Clone the repository
```shell
$ cd ~/Documents
$ pwd
$ git git@github.com:jshbrntt/networkedphysics-gdc2015.git
```

### 1.2 Install build dependencies

#### 1.2.1 Install `premake`

```shell
$ brew install premake
# ...
$ brew info
==> premake: stable 5.0.0-beta1 (bottled), HEAD
Write once, build anywhere Lua-based build system
https://premake.github.io/
/usr/local/Cellar/premake/5.0.0-beta1 (6 files, 1.2MB) *
  Poured from bottle on 2022-10-04 at 16:33:35
From: https://github.com/Homebrew/homebrew-core/blob/HEAD/Formula/premake.rb
License: BSD-3-Clause
# ...
$ premake5 --version
premake5 (Premake Build Script Generator) 5.0.0-beta1
```

#### 1.2.2 Install `glfw`

```shell
$ brew install glfw
# ...
$ brew info glfw
==> glfw: stable 3.3.8 (bottled), HEAD
Multi-platform library for OpenGL applications
https://www.glfw.org/
/usr/local/Cellar/glfw/3.3.8 (14 files, 462KB) *
  Poured from bottle on 2022-10-04 at 16:47:21
From: https://github.com/Homebrew/homebrew-core/blob/HEAD/Formula/glfw.rb
License: Zlib
# ...
```

#### 1.2.2 Install `glew`

```shell
$ brew install glew
# ...
$ brew info glew
==> glew: stable 2.2.0 (bottled), HEAD
OpenGL Extension Wrangler Library
https://glew.sourceforge.io/
/usr/local/Cellar/glew/2.2.0_1 (38 files, 3.4MB) *
  Poured from bottle on 2022-10-04 at 16:57:42
From: https://github.com/Homebrew/homebrew-core/blob/HEAD/Formula/glew.rb
License: BSD-3-Clause
# ...
```

#### 1.2.3 Install `jansson`

```shell
$ brew install jansson
# ...
$ brew info jansson
==> jansson: stable 2.14 (bottled)
C library for encoding, decoding, and manipulating JSON
https://digip.org/jansson/
/usr/local/Cellar/jansson/2.14 (11 files, 210KB) *
  Poured from bottle on 2022-10-04 at 16:58:22
From: https://github.com/Homebrew/homebrew-core/blob/HEAD/Formula/jansson.rb
License: MIT
# ...
```

### 1.2.4 Install `glm`

```shell
$ brew install glm
# ...
$ brew info jansson
==> glm: stable 0.9.9.8 (bottled), HEAD
C++ mathematics library for graphics software
https://glm.g-truc.net/
/usr/local/Cellar/glm/0.9.9.8 (1,341 files, 18.8MB) *
  Poured from bottle on 2022-10-04 at 16:58:51
From: https://github.com/Homebrew/homebrew-core/blob/HEAD/Formula/glm.rb
License: MIT
# ...
```

#### 1.2.5 Install `ode`

So the original release instructions said to install `ode` with `brew`, but I found when I did this I ended up with a compiler error of `You can only #define dSINGLE or dDOUBLE, not both.`.

I think this is because there is already a version of `ode` present in the `external` directory and the brew installed version and that version were conflicting some how.

So instead I opted for not installing `ode` via `brew` and install compiling and installing the version provided in the `external` directory which can be done like so.

```shell
$ cd external/ode
$ ./configure
# ...
$ make
# ...
$ make all
# ...
```

### 1.3 Adding the `pm` alias

You will need to add the alias to your shell's RC file.

```shell
# Bash
$ echo "alias pm='premake5'" >> ~/.bashrc

# Zsh
$ echo "alias pm='premake5'" >> ~/.zshrc
```

### 1.4 Compiling the code

```shell
# Generate GNU makefiles using premake5
$ pm gmake

# Compile all the project targets
$ make all

# Run the tests
$ pm test
```

(Effectively just following the build instructions present in [`external/ode/INSTALL.txt`](https://github.com/jshbrntt/networkedphysics-gdc2015/blob/release/external/ode/INSTALL.txt#L34))

## 2. Running the demos

The source code is structured into separate demos. Each demo corresponds roughly to a section of the talk.

This allows me to easily try out new concepts and move from exploring one area like snapshot interpolation,
to another like delta compression or state synchronization while having code that is reasonably clean and
specific to what I am currently working on. 

It also avoids breaking old code when I switch from one section to another section, because I start from 
a clean slate and put the old code off to the side each time I switch to a new thing.
cat /System/Library/CoreServices/SystemVersion.plist
I setup an alias for "premake4" to "pm" on my systems to make it faster to iterate.  I'll use that alias from now on.

You can run these demos from the command line with shortcuts individually:

    1. "pm cubes" -- run the cubes demo in single player.

    2. "pm lockstep" -- run the deterministic lockstep demo

    3. "pm snapshot" -- run the snapshot interpolation demo

    4. "pm compression" -- run the demo of lossy compression techniques (quantize etc.)

    5. "pm delta" -- run the delta compression demo

    6. "pm sync" -- run the state synchronization demo

There is also a bunch of other stuff in there as supporting code, poke around the premake4.lua file and you'll find it.

## 3. Contols

Across all demos you can control the player cube as follows:

    arrow keys -- strafe
    space      -- jump/hover
    z          -- katamari

You can also reset the current demo at any time by pressing "ESCAPE"

Within each demo you can pick different modes pressing the 1,2,3,4,5 keys:

In the lockstep demo:

    1. Deterministic mode (default)
    2. Non-deterministic mode
    3. TCP 100ms round-trip, 1% packet loss
    4. TCP 200ms round-trip, 2% packet loss
    5. TCP 250ms round-trip, 5% packet loss
    6. UDP 250ms round-trip, 25% packet loss

In the snapshot demo:

    1. Snapshots at 60 packets per-second (default)
    2. Snapshots at 60 packets per-second (with jitter)
    3. Snapshots at 10 packets per-second
    4. Linear interpolation at 10 packets per-second
    5. Hermite interpolation at 10 packets per-second

In the compression demo:

    1. Uncompressed (default)
    2. Compress orientation
    3. Compress sinear velocity
    4. At rest flag
    5. Don't send velocity at all
    6. Compress position
        
In the delta demo:

    1. Not changed bit (default)
    2. Changed index
    3. Relative index
    4. Relative position
    5. Relative orientation

In the state synchronization sync demo:

    1. Input and State (default)
    2. Quantize
    3. Smoothing

And that's all there is to it. Enjoy!
