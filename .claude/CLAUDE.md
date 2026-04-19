# Project Instructions

## C++ Coding Standards

- write .md files into a subfolder ./docs/
- never remove ToDo comments unless fully implemented
- write private functions at the end of the file after public functions
- write modern `C++23` code according to latest standard
- The source code of original hardware and firmware are in this repo root at:
  - `mayhem-firmware`: the firmware for the Mayhem device. This is the device we write an emulator for in this project (EmuHem)
  - `hackrf`: the hardware designs and software for it
  - `PortaRF`: the hardware designs for the latest PortaRF device
  

- don't use `git commit` and `git add` unless explicitly instructed


## Testing
- use LLDB to fix crashes via stack trace: `lldb -o "settings set auto-confirm true" -o "process launch -- my_binary" -k "bt" -k "quit" -- ./tests/tst_TcpConnect 2>&1`