param(
    [ValidateSet("Release", "Debug", "RelWithDebInfo")]
    [string]$Configuration = "Release"
)

$vsInstallPath = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$vcvars = Join-Path $vsInstallPath "VC\Auxiliary\Build\vcvarsall.bat"
$buildDir = "$PSScriptRoot\build\$Configuration"

# 1. Configure the CMake project
# This sets up the MSVC environment and generates the Ninja build files
cmd /c "`"$vcvars`" x64 && cmake -G Ninja -DCMAKE_BUILD_TYPE=$Configuration -B `"$buildDir`" -S `"$PSScriptRoot`""
if ($LASTEXITCODE -ne 0) {
    Write-Error "CMake configuration failed with exit code $LASTEXITCODE"
    exit $LASTEXITCODE
}

# 2. Build the project
# We need to set up the MSVC environment again for the build step using Ninja
cmd /c "`"$vcvars`" x64 && ninja -C `"$buildDir`""

if ($LASTEXITCODE -eq 0) {
    & "$buildDir\minigui.exe"
} else {
    Write-Error "Build failed with exit code $LASTEXITCODE"
    exit $LASTEXITCODE
}
