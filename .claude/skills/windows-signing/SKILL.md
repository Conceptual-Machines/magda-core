---
name: windows-signing
description: Code-sign the MAGDA Windows installer with the Certum cloud certificate (SimplySign). Use when releasing on Windows, when the signing job in release.yml needs attention, or when troubleshooting "cert not found" / SmartScreen warnings.
user_invocable: true
---

# Windows Code Signing

MAGDA's Windows installer is signed with **Authenticode** using the Certum
**"Open Source Developer Luca Romagnoli"** certificate (OV class) held in the
cloud via **SimplySign**. There is no Windows "notarization" — that's an Apple
concept; on Windows you sign the binary and embed a trusted timestamp.

Because it's OV (not EV), SmartScreen trust builds up gradually as downloads
accumulate; it is not instantly clean on day one. Nothing to configure for that.

## Key facts

| | |
| --- | --- |
| Cert thumbprint | `715B3557D43AF8FC23297999C0541C07E1B61319` |
| Issuer | Certum Code Signing 2021 CA |
| Expires | 2027-06-29 |
| Timestamp server | `http://time.certum.pl` (mandatory) |
| signtool | `C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\signtool.exe` |
| Signing script | `packaging/windows/sign-release.ps1` |

The timestamp keeps signatures valid after the cert expires, so re-signing old
builds is never needed.

## Toolchain (one-time setup)

You need exactly two SimplySign pieces — **not** SafeSign (that's for physical
USB cards):

1. **SimplySign Mobile** (phone) — activate once via the "Regaining Access to
   the SimplySign Service" email: open its link, enter the activation/secret
   code on the web page, then scan the resulting QR with the app's *QR code*
   activation. After that the app shows a **Token ID + rolling 6-digit OTP**.
   Use **"Generate token only"**, not "Sign and generate token".
2. **SimplySign Desktop** (the signing machine) — install with only
   "SimplySign Desktop" ticked (uncheck "proCertum SmartSign"). Log in with the
   email + Token ID/OTP from the phone. This exposes the cloud cert as a virtual
   smart card; it then appears in `Cert:\CurrentUser\My`.

Confirm the cert is reachable:

```powershell
Get-ChildItem Cert:\CurrentUser\My | Where-Object Thumbprint -eq '715B3557D43AF8FC23297999C0541C07E1B61319' |
  Format-List Subject, NotAfter, @{N='HasPrivateKey';E={$_.HasPrivateKey}}
```

`HasPrivateKey : True` means signing will work.

## The hard constraint

SimplySign requires an **interactive OTP** from the phone each session, so
signing **cannot** run on a GitHub-hosted runner or as a background service —
it must run inside the logged-in desktop session where SimplySign Desktop holds
the cert. One OTP login covers many signs for the session.

## Releasing (the CI path)

`release.yml` has a `sign-windows` job (tag pushes only) on the self-hosted
runner `[self-hosted, Windows, magda-luca]`. It downloads the unsigned build
artifact, provisions NSIS, unpacks the MuseHub source bundle, and runs
`sign-release.ps1 -WaitMinutes 30`, then publishes the signed installer via
`create-release`.

Per-release steps:

1. Make sure the runner is running **interactively in your logged-in session**
   (not the `NT AUTHORITY\NETWORK SERVICE` service — that session can't see the
   cert). Either run `run.cmd` from the runner folder or have it auto-start at
   logon.
2. Cut the release tag (see the [release](../release/SKILL.md) skill).
3. When the `sign-windows` job prints "Waiting for SimplySign login…", open
   SimplySign Desktop and enter the OTP from your phone. The job proceeds the
   instant the cert appears (within the 30-minute window). Order doesn't matter
   — you can log in before or after pushing the tag.

## Signing manually (no CI)

On a machine logged into SimplySign Desktop, point the script at a staging dir
that holds `MAGDA.exe`, `magda_plugin_scanner.exe` and `magda-installer.nsi`
(an extracted `MAGDA-<version>-MuseHub-Source.zip`, or a local build's installer
staging dir):

```powershell
packaging\windows\sign-release.ps1 -StageDir <dir> -Version <x.y.z> -Makensis <path-to-makensis.exe>
```

It signs both exes, rebuilds the installer (makensis runs with the working
directory set to the stage so `OutFile` lands there), signs it, verifies the
chain, and writes `MAGDA-<version>-Windows-x86_64-Setup.exe` + `.sha256`.

To sign a single file directly:

```powershell
signtool sign /sha1 715B3557D43AF8FC23297999C0541C07E1B61319 /fd sha256 /tr http://time.certum.pl /td sha256 /v <file>
signtool verify /pa /v <file>
```

The ONNX Runtime and libxml2 DLLs are vendor-signed already — only sign the two
MAGDA exes and the installer.

## Troubleshooting

- **"Signing cert … not found"** — SimplySign Desktop isn't logged in, or the
  job is running in a non-interactive session (the NETWORK SERVICE service).
  Log in / run the runner in-session.
- **The OTP can't be automated** — it's the deliberate MFA floor for this cert
  class. Full unattended signing would need an EV/HSM service (Azure Trusted
  Signing, SignPath), not this Certum cloud cert.
- **The cert expired (after 2027-06-29)** — already-shipped builds stay valid
  because they're timestamped; reissue/renew the cert in the Certum panel and
  update the thumbprint here and in `sign-release.ps1`.
