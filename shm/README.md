# Shared Memory (SHM) Library

## Directory Structure
- `/shm/` - Main directory for shared memory library
  - `/shm/include/` - Header files directory
  - `/shm/lib/` - Library files directory
  - `/shm/exp/` - Experimental implementations directory
    - `/shm/exp/shm_mptcp/` - MPTCP-related experiments
    - `/shm/exp/shm_cs/` - Client-remote server experiments
    - `/shm/exp/local_cs/` - Local client-server experiments

## Key Files
### `shm_sock.c`
- **Core Implementation**: Provides the foundational logic for shared memory sockets, enabling high-performance inter-process communication (IPC) via shared memory.
- **Features**:
  - Implements socket-like APIs (`shm_socket`, `shm_bind`, `shm_connect`, etc.) for seamless integration with existing socket-based applications.
  - Manages shared memory regions for data transfer between processes.
  - Supports atomic synchronization mechanisms to ensure thread-safe operations.
  - Includes error handling and logging for debugging.
- **Dependencies**: Requires `shm_sock.h` for definitions and `shm_debug.h` for debugging utilities.

### `hook.c`
- **Hook Implementation**: Intercepts and overrides standard socket operations (e.g., `socket`, `bind`, `send`, `recv`) to redirect them to shared memory equivalents.
- **Features**:
  - Uses dynamic linking (e.g., `LD_PRELOAD`) to inject hooks into the target application.
  - Transparently replaces socket calls with `shm_sock.c` implementations, minimizing code changes in the application.
  - Provides performance metrics and logging for intercepted operations.
- **Use Case**: Ideal for legacy applications where modifying the codebase to use `shm_sock.c` directly is impractical.

### Other Files
- `shm_sock.h` - Main header file defining shared memory socket interfaces and data structures.
- `shm_debug.h` - Debugging utilities and macros for the SHM library.
- `Makefile` - Build configuration for compiling the SHM library.

## Build Artifacts
if complile successfully
- `hook.o` - Compiled object file for hook implementation
- `shm_sock.o` - Compiled object file for shared memory socket implementation