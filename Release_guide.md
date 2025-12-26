# X4reader — Release Guide

This guide explains how to install X4reader on your Xteink X4 via the Launcher (OTA from within the device). Make sure your device is already running the Launcher.

## Prerequisites
- Launcher installed and working on your X4 (see Launcher guide if you need to flash it first).
- Stable Wi-Fi connection for the device.
- A GitHub release or hosted package for X4reader that the Launcher can fetch.

## OTA install via Launcher
1) Open Launcher on your X4 and ensure it’s connected to Wi-Fi.
2) Navigate to the Apps/Updates section (exact wording may vary by Launcher build).
3) Locate **X4reader** in the available apps list. If it doesn’t show automatically, add the feed/source URL for the X4reader release provided in the release notes.
4) Select **Install** (or **Update**) for X4reader. The Launcher will download and apply the package OTA.
5) Wait for the installation to complete; do not power off during this step.
6) Launch X4reader from the Launcher menu once the install finishes.

## Verify after install
- X4reader should open from the Launcher apps menu.
- Confirm storage/SD access works for your books.
- If configured, check network features (sync/metadata) behave as expected.

## Troubleshooting
- If X4reader doesn’t appear in the apps list, verify the feed/source URL is correct and the device is online.
- If install fails, retry after rebooting the device and confirming Wi-Fi.
- For persistent issues, re-open the Launcher updates screen and re-trigger install; if still failing, try clearing any cached listings in Launcher (if available) or reflash Launcher to a known good build.
