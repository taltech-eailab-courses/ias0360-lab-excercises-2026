# IAS0360 Lab Exercises 2026

## Setting Up Your Own PC

These instructions will help you set up the build environment needed to build and deploy applications to the Pico using a Docker container.

### Git & SSH Setup

Copy `.env.example` to `.env` and fill in your own details:

```bash
cp .env.example .env
```

- `GIT_USER_NAME` / `GIT_USER_EMAIL` - used to run `git config --global` inside the container.
- `GITHUB_USERNAME` - your GitHub username, for reference.
- `SSH_PRIVATE_KEY` - an SSH private key (e.g. `cat ~/.ssh/id_ed25519`) authorized on your GitHub account, pasted in quotes exactly as it appears including the `BEGIN`/`END` lines.

`.env` is gitignored - it holds a real private key once filled in, so never commit it. Every time the container starts, `entrypoint.sh` reads `.env` and, if present, sets your git identity and installs the key fresh with `600` permissions in the container's own filesystem (not on your disk - some host filesystems, e.g. NTFS drives, silently ignore `chmod` and would otherwise leave the key unusable). Git is pointed at it via `core.sshCommand`, plus GitHub's host key in `known_hosts`, so `git clone git@github.com:...` works right away. This is skipped entirely if `.env` is missing.

Once the SSH key is in place, `entrypoint.sh` also clones your homework submission repos into `~/submission` - i.e. `submission/` in the repo root, since that's your home directory inside the container. `submission/` is just a holding folder: each repo below lands in its own subdirectory named after the repo, so they don't collide.

- `git@github.com:taltech-eailab-courses/ias0360-home-assignment-1-submission-2026.git` → `submission/ias0360-home-assignment-1-submission-2026/`
- `git@github.com:taltech-eailab-courses/ias0360-home-assignment-2-submission-2026.git` → `submission/ias0360-home-assignment-2-submission-2026/`
- `git@github.com:taltech-eailab-courses/ias0360-home-assignment-3-submission-2026.git` → `submission/ias0360-home-assignment-3-submission-2026/`

Cloning only happens once per repo; if a repo's subdirectory already exists, it's left alone on subsequent container starts. Everything under `submission/` is its own git repository and is gitignored here - none of it is tracked as part of this repo.

### Building the Docker Image

Once you've cloned this repo, make sure you're in the repo root in a console/terminal, then run:

```bash
chmod +x build_in_docker.sh
./build_in_docker.sh rebuild
```

This will initially build and install a Docker image named `ias0360-2026` on your PC, then open an interactive shell by spinning up a container from that image. The image itself is defined by the `Dockerfile` and `entrypoint.sh` in `own_pc_setup/`, which `build_in_docker.sh` uses as the Docker build context.

- Running `./build_in_docker.sh` again (without arguments) always starts a fresh container - if one under this name is already running (e.g. left over from another user on a shared PC), it's stopped and replaced rather than reused.
- Passing the `shell` argument opens an additional terminal into the container you already have running (e.g. to run Jupyter and a Pico build side by side); it fails if none is running - start one with a plain `./build_in_docker.sh` first.
- Passing the `stop` argument will stop the running container.
- Passing the `rebuild` argument will stop the running container, delete the image, and rebuild it from scratch.

The script spins up the container with the minimum requirements needed for development: network connectivity and access to the host's USB devices, so a Pico connected to your PC can be accessed from within the container.

This image mounts the directory where you ran `./build_in_docker.sh` (this repo's root) as the home directory inside the container. This means you can edit local files in that directory from your PC or from within the container, and changes will persist even if the container is stopped, removed, or the image is rebuilt.

Since this directory is mounted as your home directory inside the container, you can create additional folders here for your own applications (e.g. `my_project/`) alongside the lab folders. Anything placed here will have access to the same `build_in_docker.sh` and `flash.sh` scripts already present, without needing to rebuild the image.

### Building a Pico Application

A minimal Pico example is provided at `blink_example`, targeting the RP2040 Pico W. Follow the steps below to build it.

```bash
cd blink_example
mkdir build
cd build
cmake ..
make -j$(nproc)
```

#### Flashing the Application

1. Unplug the Pico from your PC (if it's already connected).
2. Hold down the **BOOTSEL** button, plug the Pico back in, then release the button about 2 seconds later.

The Pico should now be in bootloader mode. You can verify this by running `lsusb` - the device should appear as **"Raspberry Pi RP2 Boot"** instead of **"Raspberry Pi Pico"**.

Once confirmed, run:

```bash
cd ~
chmod +x flash.sh   # only needed once per container
./flash.sh blink_example/build/blink.uf2
```

This will flash your application onto the Pico while it's in boot mode.

Subsequent flashes of this example won't require manually putting the Pico into boot mode - the flashed application already includes the headers needed to programmatically force the Pico into bootloader mode via the command line, using `picotool`.

If you'd like to add this automatic BOOTSEL functionality to your own application, follow the steps below.

#### Creating Your Own Application

You don't need to rebuild the Docker image to start a new project - the image already contains everything needed to build and flash. You have two options:

- **Recommended:** Create your new application folder directly inside the repo root (next to `blink_example/` and the lab folders). This way you can use the existing `flash.sh` script directly, since it's already in `~`.
- **Alternative:** If you'd rather keep your project elsewhere (e.g. its own separate repo/directory), copy `build_in_docker.sh` and `flash.sh` into that directory. Since these scripts only need `own_pc_setup/Dockerfile` when the `ias0360-2026` image doesn't exist yet or you pass `rebuild`, running them from a different directory works for normal start/stop once the image has been built at least once from here - for `rebuild` to work standalone, copy `own_pc_setup/` alongside the scripts too.

### Enabling BOOTSEL Over USB (No Physical Button) in your application

Add the following to your application to enable this feature.

#### 1. CMakeLists.txt

After `add_executable(<target> ...)`:

```cmake
pico_enable_stdio_usb(<target> 1)
pico_enable_stdio_uart(<target> 0)
```

#### 2. Source File

Call `stdio_init_all()` once at the top of `main()` (before anything else) to bring up the USB device:

```c
#include "pico/stdlib.h"

int main() {
    stdio_init_all();
    // ... rest of the program
}
```

## ML Model Development (TensorFlow / TFLite Micro)

The image ships a Python venv (`/opt/venv`, already on `PATH`) with TensorFlow, OpenCV (headless), NumPy, scikit-learn, and JupyterLab for training and converting TinyML models to run on the Pico via TFLite Micro.

To launch notebooks, from inside the container:

```bash
jupyter lab --ip=0.0.0.0 --no-browser --allow-root
```

`build_in_docker.sh` publishes the container's port 8888 to the same port on your host (`-p 8888:8888`), so once Jupyter prints its URL, open `localhost:8888` directly in your host browser - no extra setup needed, and this works the same on Linux, macOS, and Windows. Alternatively, attach VS Code to the running container ("Dev Containers: Attach to Running Container") and point its Jupyter extension at that same server as a remote kernel.

Use `xxd -i model.tflite > model.h` to turn a converted `.tflite` model into a C byte array for embedding in a Pico firmware project.
