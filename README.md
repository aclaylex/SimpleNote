# SimpleNote

A minimal Notepad clone for Windows, built directly on the Win32 API with no runtime dependencies beyond stock Windows DLLs. Designed to stay as small as possible.

## Features

- New / Open / Save / Save As
- Find, Replace, Find Next
- Undo / Redo
- Word wrap toggle
- Custom background color, with automatic dark mode detection
- Optional character count
- Settings (window size/position, word wrap, character count, background color) persist between sessions
- Word-aware Ctrl+Backspace / Ctrl+Delete

## Download

Grab `SimpleNote.exe` directly from this repo — no installer required. It runs on any modern Windows 10/11 machine with no extra setup.

## Building from source

Requires the Visual Studio Build Tools (run from the "x64 Native Tools Command Prompt"):

    build.bat

This produces `SimpleNote.exe`.

## Credits

<a href="https://www.flaticon.com/free-icons/notepad" title="notepad icons">Notepad icons created by Freepik - Flaticon</a>
