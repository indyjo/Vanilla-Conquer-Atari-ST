# COMMONLIB

This directory contains platform-agnostic code that can be shared between different platform implementations (Windows, Atari ST, etc.).

## Files

- **wwstd.h** - Standard utility macros, templates, and color definitions
  - Template functions (ABS, MIN, MAX, etc.)
  - Bit manipulation macros
  - ColorType enum
  - Utility macros (GET_SIZE, LOW_WORD, HIGH_WORD, MAKE_LONG)
  - Conditionally includes Windows headers only on Windows builds

- **structs.h** - Structure definitions (currently empty placeholder)

- **defines.h** - Platform-agnostic constants
  - USER_TIMER_FREQ

- **externs.h** - External variable declarations
  - NoTimer, NoKeyBoard

- **coorda.h / coorda.cpp** - Fixed-point coordinate conversion functions
  - Cardinal_To_Fixed()
  - Fixed_To_Cardinal()
  - Pure math functions, no OS dependencies

## Usage

Include files from this directory using:
```cpp
#include "commonlib/wwstd.h"
#include "commonlib/coorda.h"
```

Platform-specific libraries (WIN32LIB, ATARILIB) should include these common files rather than maintaining their own copies.

