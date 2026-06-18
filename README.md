# Blinq Messenger

Blinq Messenger is a Windows Qt messenger for local LAN chat and account-based Internet Mode chat.

## Features

- LAN Mode contact discovery and private chat.
- Internet Mode sign-in, contact search, presence, private messages, queued offline text messages, typing state, read receipts, and account controls.
- Public LAN chat, file sending, inline images, drawing pad, whistles, custom themes, blocked users, favorites, and contact groups.
- First-run welcome window, help page, installer license, update checker, and backup/restore for settings and chat history.

## Build

This project is built with Qt 6 and CMake on Windows.

```powershell
C:\Qt\Qt\Tools\CMake_64\bin\cmake.exe --build build\Desktop_Qt_6_11_1_MinGW_64_bit-Debug
```

For a clean release package and installer:

```powershell
powershell.exe -ExecutionPolicy Bypass -File deploy\windows\package-release.ps1
```

The installer is generated at:

```text
deploy/windows/installer/BlinqMessengerSetup-1.0.0.exe
```

## Updates

The app checks this file for update metadata:

```text
https://raw.githubusercontent.com/ypd11/Blinq-Messenger/main/update.json
```

When releasing a new version:

1. Update `project(BlinqMessenger VERSION ...)` in `CMakeLists.txt`.
2. Build the installer with `deploy/windows/package-release.ps1`.
3. Create a GitHub Release and upload the installer as a release asset.
4. Update `update.json` on `main` with the new version, changelog, and installer URL.

Example:

```json
{
  "version": "1.0.1",
  "releaseDate": "2026-06-18",
  "installerUrl": "https://github.com/ypd11/Blinq-Messenger/releases/download/v1.0.1/BlinqMessengerSetup-1.0.1.exe",
  "changelog": [
    "Added update checker.",
    "Added backup and restore."
  ]
}
```

## Server

The Internet Mode server source is in:

```text
deploy/blinq-server/server.js
```

After server changes, deploy it to the VPS and restart the service:

```cmd
scp deploy\blinq-server\server.js root@<vps-ip>:/opt/blinq-server/server.js
ssh root@<vps-ip> "chown blinq:blinq /opt/blinq-server/server.js && systemctl restart blinq-server"
```

## License

Blinq Messenger is freeware by Exe Innovate. See `LICENSE.txt`.
