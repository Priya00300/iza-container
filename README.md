# 🎯 Iza - Minimal Container Runtime

Iza is a minimal container runtime written in C++ that demonstrates the core concepts behind Docker, Podman, and other container technologies. It directly uses Linux kernel features like namespaces and cgroups to create isolated environments.

## 🚀 Current Status: Phase 1 Complete

✅ **Basic Process Isolation** - Create containers with isolated PID, mount, UTS, IPC, and network namespaces  
✅ **Filesystem Isolation** - Custom rootfs with chroot  
✅ **Basic Command Execution** - Run commands inside isolated containers  

## 🛠️ Building and Running

### Prerequisites
- Linux system (WSL2 works perfectly)
- Root privileges (required for namespaces and chroot)
- g++ compiler with C++17 support

### Build
```bash
# Clone or create the project files
make clean && make

# Or build with debug symbols
make debug
```

### Usage
```bash
# Basic container with bash shell
sudo ./iza run /bin/bash

# Run a specific command
sudo ./iza run /bin/ls /

# Check container isolation
sudo ./iza run /usr/bin/whoami
```

## 🧪 Testing Container Isolation

Once inside a container, you can verify the isolation:

```bash
# Check hostname (should be 'iza-container')
hostname

# Check processes (should only show container processes)
ps aux

# Check filesystem (should show minimal container filesystem)
ls /

# Check current user
whoami
```

## 🏗️ Architecture Overview

```
┌─────────────────────────────────────────────────────────────┐
│                        Host System                          │
│  ┌───────────────────────────────────────────────────────┐  │
│  │                 Iza Parent Process                    │  │
│  │  • Parses command line arguments                     │  │
│  │  • Sets up container environment                     │  │
│  │  • Creates child process with clone()                │  │
│  └───────────────────────────────────────────────────────┘  │
│                              │                              │
│                              ▼                              │
│  ┌───────────────────────────────────────────────────────┐  │
│  │              Container (Child Process)                │  │
│  │                                                       │  │
│  │  Namespaces:                                          │  │
│  │  • PID - Isolated process IDs                        │  │
│  │  • Mount - Isolated filesystem                       │  │
│  │  • UTS - Custom hostname                             │  │
│  │  • IPC - Isolated inter-process communication       │  │
│  │  • Network - Isolated network stack                 │  │
│  │                                                       │  │
│  │  Root Filesystem: /tmp/iza_rootfs                    │  │
│  │  • /bin/bash, /bin/ls, /usr/bin/whoami              │  │
│  │  • /proc (mounted)                                   │  │
│  │  • /tmp (tmpfs)                                      │  │
│  │  • Basic /etc/passwd                                 │  │
│  └───────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────┘
```

## 🔍 How It Works

### 1. Process Creation with clone()
Unlike `fork()` which creates an identical copy, `clone()` allows us to create a new process with isolated namespaces:

```cpp
int clone_flags = CLONE_NEWPID |    // PID namespace
                 CLONE_NEWNS |     // Mount namespace  
                 CLONE_NEWUTS |    // UTS namespace (hostname)
                 CLONE_NEWIPC |    // IPC namespace
                 CLONE_NEWNET |    // Network namespace
                 SIGCHLD;          // Send SIGCHLD to parent when child exits
```

### 2. Filesystem Isolation
- Creates a minimal rootfs in `/tmp/iza_rootfs`
- Uses `chroot()` to change the root directory
- Mounts `/proc` and `/tmp` filesystems
- Copies essential binaries and libraries

### 3. Namespace Isolation
- **PID Namespace**: Container sees its own process tree starting from PID 1
- **Mount Namespace**: Container has its own filesystem view
- **UTS Namespace**: Container can have its own hostname
- **IPC Namespace**: Isolated message queues, semaphores, shared memory
- **Network Namespace**: Isolated network stack (currently no connectivity)

## 🚧 Limitations (Current Phase)

- **No Network Connectivity**: Containers are isolated but can't reach the internet
- **No Resource Limits**: No memory or CPU constraints
- **Basic Rootfs**: Minimal filesystem with only a few binaries
- **No Image Management**: No pulling from registries
- **Security**: Basic implementation, not production-ready

## 🗺️ Roadmap

### Phase 2: Resource Control (Coming Next)
- [ ] Memory limits using cgroups v2
- [ ] CPU limits and scheduling
- [ ] Command line flags: `--memory`, `--cpus`

### Phase 3: Image Management
- [ ] Download and extract container images
- [ ] OverlayFS for layered filesystems
- [ ] `iza pull ubuntu:latest` command

### Phase 4: Networking
- [ ] Virtual ethernet (veth) pairs
- [ ] Network namespaces with connectivity
- [ ] NAT for internet access
- [ ] `iza run --net` flag

## 🎓 Educational Value

This project demonstrates:
- Linux system programming and kernel interfaces
- Container technology fundamentals
- Process isolation and security boundaries
- Filesystem virtualization
- Modern C++ development practices

## 🤝 Contributing

This is an educational project. Feel free to:
- Experiment with the code
- Add features from the roadmap
- Improve error handling and robustness
- Add more comprehensive testing

## ⚠️ Security Warning

This is an educational implementation. Do not use in production environments. Container security requires careful consideration of:
- Privilege escalation prevention
- Resource exhaustion protection
- Kernel vulnerability mitigation
- Secure default configurations

## 📚 References

- [Linux Containers from Scratch](https://ericchiang.github.io/post/containers-from-scratch/)
- [Container Implementation Inspiration](https://github.com/joey00072/iza)
- Linux man pages: `clone(2)`, `namespaces(7)`, `cgroups(7)`