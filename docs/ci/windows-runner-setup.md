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

When you run `.\config.cmd`, choose the option to run the runner as a Windows service. The service is then managed via Windows (no `svc.cmd` in recent runner versions).

This makes the runner start automatically on boot. The service does **not** use your user PATH; to make Ninja and MSVC visible to jobs, add a `.env` file in this directory (see Troubleshooting).

### 6. Verify it's running

```powershell
Get-Service *actions*
```

You should also see it appear at:
https://github.com/organizations/Conceptual-Machines/settings/actions/runners

---

## Troubleshooting

### `cl` not found / MSVC not in PATH / Ninja not found

The runner runs as a Windows service and does **not** use your user PATH. It does **not** run `env.cmd` or any script by default. Use one of these:

**Option A: `.env` file in the runner directory (recommended)**

Create or edit `C:\actions-runner\.env` (same folder as `config.cmd`). Set `PATH`, `LIB`, and `INCLUDE` so the runner can build with MSVC. The runner loads this when it starts.

```env
PATH=C:\Windows\System32\WindowsPowerShell\v1.0;C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.xx.xxxxx\bin\Hostx64\x64;C:\Program Files (x86)\Windows Kits\10\bin\10.0.xxxxx.0\x64;C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE;C:\Program Files\CMake\bin;C:\Program Files\Git\cmd;C:\path\to\ninja;%PATH%
LIB=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.xx.xxxxx\lib\x64;C:\Program Files (x86)\Windows Kits\10\Lib\10.0.xxxxx.0\um\x64;C:\Program Files (x86)\Windows Kits\10\Lib\10.0.xxxxx.0\ucrt\x64
INCLUDE=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.xx.xxxxx\include;C:\Program Files (x86)\Windows Kits\10\Include\10.0.xxxxx.0\um;C:\Program Files (x86)\Windows Kits\10\Include\10.0.xxxxx.0\shared;C:\Program Files (x86)\Windows Kits\10\Include\10.0.xxxxx.0\ucrt
```

- **Include PowerShell** so the runner can run workflow steps: `C:\Windows\System32\WindowsPowerShell\v1.0` (add this first in PATH).
- **Include Windows SDK** so `rc.exe` and `mt.exe` are found: `C:\Program Files (x86)\Windows Kits\10\bin\10.0.xxxxx.0\x64` — replace `10.0.xxxxx.0` with your SDK version (look in `…\Windows Kits\10\bin\`).
- **Set LIB and INCLUDE** so the linker finds `kernel32.lib` and other SDK libraries; use the same MSVC and SDK version placeholders as in PATH.
- Replace `14.xx.xxxxx` with your MSVC version (look in `…\VC\Tools\MSVC\`).
- For Ninja: if you used winget, run `(Get-Command ninja).Source` in PowerShell and use that folder.
- Keep `%PATH%` at the end so system dirs stay on PATH.

Restart the runner service after changing `.env`. In **PowerShell (as Administrator)** run: `Restart-Service -Name 'actions.runner.*' -Force`, or use the exact service name from `Get-Service *actions*`.

**Option B: Pre-job hook to run vcvars64**

If you prefer to use `vcvars64.bat` instead of hardcoding paths, use the runner’s pre-job hook:

1. Create a script, e.g. `C:\actions-runner\scripts\setup-msvc.ps1` (runner docs say keep scripts **outside** the runner app dir, so e.g. `C:\runner-scripts\setup-msvc.ps1` is safer):

```powershell
# setup-msvc.ps1 - run vcvars64 and export PATH for the job
$vsPath = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath
$vcvars = Join-Path $vsPath "VC\Auxiliary\Build\vcvars64.bat"
$tempBat = [System.IO.Path]::GetTempFileName() + ".bat"
"@`"$vcvars`" && set" | Set-Content -Path $tempBat -Encoding ASCII
$envBlock = cmd /c $tempBat
Remove-Item $tempBat -ErrorAction SilentlyContinue
foreach ($line in $envBlock) {
  if ($line -match '^PATH=(.*)$') { "PATH=$($matches[1])" | Out-File -FilePath $env:GITHUB_ENV -Encoding utf8 -Append; break }
}
```

2. In `C:\actions-runner\.env`, add (use the actual path to the script):

```env
ACTIONS_RUNNER_HOOK_JOB_STARTED=C:\runner-scripts\setup-msvc.ps1
```

3. Restart the runner service. Ensure Ninja, CMake, and Git are on **system** PATH (Option C) or add them in the same script via `GITHUB_ENV`.

**Option C: System PATH (all users)**

Add MSVC and Ninja to the machine PATH so any process (including the runner service) sees them:

- MSVC: `C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\<version>\bin\Hostx64\x64`
- Ninja: e.g. the folder from `(Get-Command ninja).Source`

Set via **PowerShell as Administrator**: `[Environment]::SetEnvironmentVariable("Path", $env:Path + ";C:\path\to\ninja;...", "Machine")`

### Runner stays "Offline" in GitHub

- Check the service is running: `Get-Service *actions*`
- Check logs: `type C:\actions-runner\_diag\Runner_*.log | Select-Object -Last 30`
- Restart (requires **PowerShell as Administrator**): `Restart-Service -Name 'actions.runner.*' -Force`

### "Cannot open ... service" when restarting

Restarting the runner service needs Administrator privileges. Open **PowerShell as Administrator** and run:

```powershell
Restart-Service -Name 'actions.runner.Conceptual-Machines-magda-core.LUCATHINKPAD' -Force
```

Use the exact service name from `Get-Service *actions*` if your runner name differs.

### Build fails with missing headers

Make sure the Windows SDK is installed (comes with the VS C++ workload). You can verify in Visual Studio Installer → Modify → Individual Components → search "Windows SDK".
