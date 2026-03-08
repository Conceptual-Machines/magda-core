# Windows Self-Hosted Runner Setup

## Prerequisites

### 1. Install Visual Studio 2022

Download [Visual Studio 2022 Community](https://visualstudio.microsoft.com/downloads/) (free).

During installation, select the **"Desktop development with C++"** workload.

### 2. Install CMake

```powershell
winget install Kitware.CMake
```

Or download from https://cmake.org/download/ — check "Add CMake to the system PATH" during install.

### 3. Install Ninja

```powershell
winget install Ninja-build.Ninja
```

### 4. Install Git

```powershell
winget install Git.Git
```

After installing, **restart your terminal** so PATH updates take effect.

### 5. Verify tools are available

Open a **new** PowerShell window and run:

```powershell
cmake --version
ninja --version
git --version
cl
```

If `cl` is not found, you need to run from a "Developer Command Prompt for VS 2022", or add MSVC to your PATH (see Troubleshooting below).

---

## Set Up the GitHub Actions Runner

### 1. Create the runner directory

```powershell
mkdir C:\actions-runner
cd C:\actions-runner
```

### 2. Download the runner

```powershell
Invoke-WebRequest -Uri https://github.com/actions/runner/releases/download/v2.322.0/actions-runner-win-x64-2.322.0.zip -OutFile actions-runner.zip
Expand-Archive -Path actions-runner.zip -DestinationPath .
Remove-Item actions-runner.zip
```

> Check https://github.com/actions/runner/releases for the latest version.

### 3. Get the registration token

1. Go to: https://github.com/Conceptual-Machines/magda-core/settings/actions/runners/new
2. Select **Windows** as the operating system
3. Copy the token shown in the configure step

### 4. Configure the runner

```powershell
.\config.cmd --url https://github.com/Conceptual-Machines/magda-core --token YOUR_TOKEN_HERE --labels self-hosted,Windows --name Windows-Runner
```

When prompted:
- **Runner group**: press Enter (default)
- **Runner name**: `Windows-Runner` (or press Enter for default)
- **Labels**: should already have `Windows` from the flag above
- **Work folder**: press Enter (default `_work`)

### 5. Install as a Windows service

```powershell
.\svc.cmd install
.\svc.cmd start
```

This makes the runner start automatically on boot.

### 6. Verify it's running

```powershell
.\svc.cmd status
```

You should also see it appear at:
https://github.com/organizations/Conceptual-Machines/settings/actions/runners

---

## Troubleshooting

### `cl` not found / MSVC not in PATH

The runner service needs access to the MSVC compiler. Two options:

**Option A: Set system-wide environment variables**

Find your VS install path (usually `C:\Program Files\Microsoft Visual Studio\2022\Community`) and add these to the system PATH:

```
C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\<version>\bin\Hostx64\x64
```

**Option B: Use a startup script**

Create `C:\actions-runner\env.cmd` with:

```cmd
@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
```

The runner will source this before executing jobs.

### Ninja not found by CMake

Ensure Ninja is in the system PATH, not just the user PATH (since the service runs as a different user).

```powershell
[Environment]::SetEnvironmentVariable("Path", $env:Path + ";C:\Users\YOU\AppData\Local\Microsoft\WinGet\Packages\Ninja-build.Ninja_Microsoft.Winget.Source_...", "Machine")
```

### Runner stays "Offline" in GitHub

- Check the service is running: `.\svc.cmd status`
- Check logs: `type _diag\Runner_*.log | Select-Object -Last 30`
- Restart: `.\svc.cmd stop` then `.\svc.cmd start`

### Build fails with missing headers

Make sure the Windows SDK is installed (comes with the VS C++ workload). You can verify in Visual Studio Installer → Modify → Individual Components → search "Windows SDK".
