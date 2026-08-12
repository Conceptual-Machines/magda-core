# Run a command, retrying it if it fails. The PowerShell half of ci-retry.sh;
# read that one for why this exists.
#
# Usage: pwsh scripts/ci-retry.ps1 <command> [args...]
#   CI_RETRY_ATTEMPTS  how many times to try   (default 3)
#   CI_RETRY_DELAY     seconds between tries   (default 15)
#
# No param() block on purpose. The commands wrapped here are cmake and vcpkg's
# bootstrap, whose arguments are full of things PowerShell would otherwise try
# to bind as parameters of this script. Reading the automatic $args takes the
# binder out of it entirely: everything after the script path is the command.

$attempts = if ($env:CI_RETRY_ATTEMPTS) { [int]$env:CI_RETRY_ATTEMPTS } else { 3 }
$delay = if ($env:CI_RETRY_DELAY) { [int]$env:CI_RETRY_DELAY } else { 15 }

if ($args.Count -eq 0) {
    Write-Host "ci-retry: nothing to run"
    exit 2
}

$exe = $args[0]
$rest = @()
if ($args.Count -gt 1) { $rest = $args[1..($args.Count - 1)] }

for ($attempt = 1; $attempt -le $attempts; $attempt++) {
    & $exe @rest
    $status = $LASTEXITCODE

    if ($status -eq 0) { exit 0 }

    if ($attempt -eq $attempts) {
        Write-Host "ci-retry: all $attempts attempts failed (exit $status): $exe $rest"
        exit $status
    }

    Write-Host "ci-retry: attempt $attempt of $attempts failed (exit $status), retrying in $delay s"
    Start-Sleep -Seconds $delay
}
