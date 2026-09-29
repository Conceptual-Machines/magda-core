# MAGDA DAW - Windows Build (PowerShell wrapper)
# Usage: .\winbuild.ps1 [debug|run|test|clean|configure]
param([string]$Command = "debug", [Parameter(ValueFromRemainingArguments)][string[]]$Rest)
& "C:\Program Files\Git\bin\bash.exe" "$PSScriptRoot\winbuild.sh" $Command @Rest
