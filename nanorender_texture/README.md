# NanoRender

A graphics rendering project built as a foundation for computer graphics course assignments. This project integrates MicroUI (immediate-mode GUI) and MiniFB (minimal framebuffer) to create a simple, lightweight graphical application framework.

## Prerequisites

This guide uses **winget** (Windows Package Manager) to streamline the installation of required tools. `winget` is built into modern versions of Windows 10 and 11, allowing you to quickly discover and install software directly from your terminal.

Open PowerShell and run the following commands to install the necessary dependencies:

### Visual Studio Build Tools (Required for C++ compilation)

* **What it does**: Provides the C++ compiler (MSVC), linker, and essential build tools.

* **Installation**:

  ```
  winget install Microsoft.VisualStudio.2022.BuildTools
  ```

* **Note**: When the Visual Studio Installer opens, you **must** select the **"Desktop development with C++"** workload before completing the installation.

### Git (Version control)

* **What it does**: Manages code versioning and collaboration.

* **Installation**:

  ```
  winget install Git.Git
  ```

### GitHub CLI (Optional but recommended)

* **What it does**: Provides command-line access to GitHub features, streamlining workflows and AI integrations.

* **Installation**:

  ```
  winget install GitHub.cli
  ```

### CMake (Build configuration)

* **What it does**: Generates build instructions for the compiler based on the `CMakeLists.txt` file.

* **Installation**:

  ```
  winget install Kitware.CMake
  ```

### Ninja (Build system)

* **What it does**: A fast, parallel build system that executes CMake's generated instructions.

* **Installation**:

  ```
  winget install Ninja-build.Ninja
  ```

* **Setup**: The installation should automatically add Ninja to your system's PATH. If it doesn't, you may need to add it manually or place the executable in your project directory.

## Building on Windows

Run the provided PowerShell script from the project root. By default, this builds the highly optimized **Release** version:

```
.\build_and_run.ps1
```

### Build Configurations

You can specify a different build configuration using the `-Configuration` parameter. The script isolates each configuration into its own separate build folder so they don't overwrite each other.

```
# Build the Debug version
.\build_and_run.ps1 -Configuration Debug

# Build the Release version with Debugging symbols
.\build_and_run.ps1 -Configuration RelWithDebInfo
```

Here is the difference between the available configurations:

* **Release (Default)**: The compiler aggressively optimizes the code for maximum speed and strips out all debugging symbols. The resulting executable is fast and small. Use this for final testing and performance measurement.

* **Debug**: The compiler disables most optimizations and embeds full debugging symbols. The application will run slower, but you can attach a debugger (like Visual Studio or VS Code) to step through code line-by-line, inspect variables, and precisely locate crashes.

* **RelWithDebInfo** (Release with Debug Information): A hybrid approach. The code is heavily optimized for speed, but debugging symbols are still generated alongside it. This is highly useful for profiling performance or investigating bugs that only manifest in optimized code. Note that stepping through this code in a debugger can sometimes feel erratic because the compiler has reordered or removed instructions during optimization.

### How the Build Script Works

The script automates the compilation pipeline:

1. **Locates Visual Studio**: Uses `vswhere.exe` to find the latest Visual Studio installation with C++ tools.

2. **Sets up the compiler environment**: Runs `vcvarsall.bat x64` to configure the necessary environment variables for compilation.

3. **Configures with CMake**: Generates Ninja build files customized for the requested `$Configuration` mode.

4. **Builds with Ninja**: Compiles the project using parallel processing inside the target configuration folder.

5. **Runs the executable**: Launches the compiled application automatically if the build succeeds.

#### Why `vcvarsall.bat`?

The MSVC compiler requires several environment variables to be set correctly (e.g., paths to include files and libraries). `vcvarsall.bat` is an official Microsoft batch script that initializes these variables. Without it, the compiler cannot locate its standard library dependencies.

#### How the script finds it

Instead of hardcoding a path that varies between machines, the script queries `vswhere.exe`:

```
$vsInstallPath = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
```

This reliably locates the latest installed version containing the required C++ toolchain.

### Building on Other Operating Systems

For macOS or Linux, you will need an equivalent shell script. You can easily prompt an AI assistant to generate a standard Bash build script for CMake and Ninja on your specific OS.

## Understanding the Build Process

### CMake Workflow

CMake is a cross-platform meta-build system. It reads `CMakeLists.txt` and generates platform-specific files. The workflow consists of two primary stages:

1. **Configure**: Reads `CMakeLists.txt` and prepares the build environment. This includes fetching dependencies and verifying environment variables. It caches this information in `CMakeCache.txt`.

   * *Dependency Management*: This project uses `FetchContent` to automatically download and build **MicroUI** (immediate-mode GUI) and **MiniFB** (minimal framebuffer).

2. **Build**: Invokes the underlying build system (Ninja) to compile the code. Dependencies are built in the target `_deps/` folder and statically linked to your final executable. Ninja ensures only modified files are recompiled, saving time.

*Note: `build_and_run.ps1` currently runs both configuration and building every time. In a larger project, you would typically only re-configure when adding new files or changing dependencies.*

### The Build Directory Structure

Because the script supports multiple configurations, build artifacts are separated into subdirectories.

```
build/
├── Release/                 # Created when using the default Release configuration
│   ├── CMakeCache.txt       # CMake configuration cache
│   ├── build.ninja          # Build instructions for Ninja
│   ├── _deps/               # External dependencies
│   └── minigui.exe          # Final compiled application
└── Debug/                   # Created when using -Configuration Debug
    └── ...                  # Separate cache, dependencies, and executable
```

## Project Structure

```
.
├── CMakeLists.txt              # Build configuration rules and dependencies
├── build_and_run.ps1           # Windows automation script
├── README.md                   # Project documentation
├── src/
│   ├── main.cpp                # Application entry point
│   ├── ui_bridge.h             # UI layer interface definitions
│   ├── ui_renderer.cpp         # UI rendering implementation logic
│   └── ui_renderer.h           # UI renderer header definitions
└── build/                      # Generated build artifacts (ignored by Git)
```

## Committing and Pushing Changes

### Repository Layout

This project is part of a monorepo. The repository root is one folder up from this directory. Follow this workflow to save your work:

1. Navigate to the repository root (parent directory):

   ```
   cd ..
   ```

2. Check your modified files:

   ```
   git status
   ```

3. Stage your changes (this stages everything not in `.gitignore`):

   ```
   git add .
   ```

4. Commit with a descriptive message:

   ```
   git commit -m "Description of your changes"
   ```

5. Push to your remote repository:

   ```
   git push
   ```

### Important: `.gitignore` Rules

**Never commit build artifacts!** Your `.gitignore` should actively exclude:

* The `build/` directory

* Binaries (`*.exe`, `*.dll`, `*.lib`)

* Object files (`*.o`, `*.obj`)

* IDE-specific folders (`.vs/`, `.vscode/`, `.idea/`)

Always verify your staged files with `git status` before committing to ensure build files haven't slipped through.

For questions or issues with the build process, consult your course materials or share your specific error messages with an AI assistant