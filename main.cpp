#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <fstream>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sched.h>
#include <signal.h>
#include <filesystem>

class Arguments {
public:
    std::string memory_limit = "";  // e.g., "100m", "1g"
    std::string cpu_limit = "";     // e.g., "1", "0.5"
    std::vector<std::string> command;
    bool valid = false;
    
    bool parse(int argc, char* argv[]) {
        if (argc < 3) {
            show_usage();
            return false;
        }
        
        if (std::string(argv[1]) != "run") {
            std::cerr << "Error: Only 'run' command is supported\n";
            show_usage();
            return false;
        }
        
        int i = 2;
        while (i < argc) {
            std::string arg = argv[i];
            
            if (arg == "--memory" && i + 1 < argc) {
                memory_limit = argv[++i];
            } else if (arg == "--cpus" && i + 1 < argc) {
                cpu_limit = argv[++i];
            } else if (arg.starts_with("--memory=")) {
                memory_limit = arg.substr(9);
            } else if (arg.starts_with("--cpus=")) {
                cpu_limit = arg.substr(7);
            } else {
                // Rest are command arguments
                while (i < argc) {
                    command.push_back(argv[i++]);
                }
                break;
            }
            i++;
        }
        
        if (command.empty()) {
            std::cerr << "Error: No command specified\n";
            show_usage();
            return false;
        }
        
        valid = true;
        return true;
    }
    
private:
    void show_usage() {
        std::cout << "Usage: iza run [OPTIONS] COMMAND [ARGS...]\n\n"
                  << "Options:\n"
                  << "  --memory LIMIT    Memory limit (e.g., 100m, 1g)\n"
                  << "  --cpus LIMIT      CPU limit (e.g., 1, 0.5)\n\n"
                  << "Examples:\n"
                  << "  iza run /bin/bash\n"
                  << "  iza run --memory 100m /bin/bash\n"
                  << "  iza run --memory 50m --cpus 1 stress --vm 1 --vm-bytes 100m\n";
    }
};

class CgroupManager {
private:
    std::string cgroup_name;
    std::string cgroup_path;
    bool created = false;
    
public:
    CgroupManager() {
        // Generate unique cgroup name
        cgroup_name = "iza-" + std::to_string(getpid()) + "-" + std::to_string(time(nullptr));
        cgroup_path = "/sys/fs/cgroup/" + cgroup_name;
    }
    
    ~CgroupManager() {
        cleanup();
    }
    
    int create_cgroup() {
        std::cout << "[DEBUG] Creating cgroup: " << cgroup_path << std::endl;
        
        // Check if cgroups v2 is available
        if (!std::filesystem::exists("/sys/fs/cgroup/cgroup.controllers")) {
            std::cerr << "Error: cgroups v2 not available. Make sure you're running on a modern Linux system.\n";
            return -1;
        }
        
        // Create cgroup directory
        if (mkdir(cgroup_path.c_str(), 0755) != 0) {
            perror("Failed to create cgroup directory");
            return -1;
        }
        
        created = true;
        
        // Enable controllers we need
        std::string controllers_file = cgroup_path + "/cgroup.subtree_control";
        std::ofstream controllers(controllers_file);
        if (!controllers.is_open()) {
            std::cerr << "Warning: Could not enable cgroup controllers\n";
            // Continue anyway - some systems may not require this
        } else {
            controllers << "+memory +cpu";
            controllers.close();
        }
        
        return 0;
    }
    
    int set_memory_limit(const std::string& limit) {
        if (!created) return -1;
        
        std::cout << "[DEBUG] Setting memory limit: " << limit << std::endl;
        
        // Parse limit (e.g., "100m" -> 104857600 bytes)
        long long bytes = parse_memory_limit(limit);
        if (bytes <= 0) {
            std::cerr << "Error: Invalid memory limit format: " << limit << std::endl;
            return -1;
        }
        
        std::string memory_max_file = cgroup_path + "/memory.max";
        std::ofstream memory_file(memory_max_file);
        if (!memory_file.is_open()) {
            std::cerr << "Error: Could not set memory limit. File: " << memory_max_file << std::endl;
            perror("fopen");
            return -1;
        }
        
        memory_file << bytes;
        memory_file.close();
        
        std::cout << "[DEBUG] Memory limit set to " << bytes << " bytes" << std::endl;
        return 0;
    }
    
    int set_cpu_limit(const std::string& limit) {
        if (!created) return -1;
        
        std::cout << "[DEBUG] Setting CPU limit: " << limit << std::endl;
        
        // Parse CPU limit (e.g., "1.5" -> 150000 quota, 100000 period)
        double cpu_cores = std::stod(limit);
        if (cpu_cores <= 0) {
            std::cerr << "Error: Invalid CPU limit: " << limit << std::endl;
            return -1;
        }
        
        // CPU quota and period (default period is 100ms = 100000 microseconds)
        int period = 100000;
        int quota = (int)(cpu_cores * period);
        
        // Set CPU quota
        std::string cpu_max_file = cgroup_path + "/cpu.max";
        std::ofstream cpu_file(cpu_max_file);
        if (!cpu_file.is_open()) {
            std::cerr << "Error: Could not set CPU limit. File: " << cpu_max_file << std::endl;
            perror("fopen");
            return -1;
        }
        
        cpu_file << quota << " " << period;
        cpu_file.close();
        
        std::cout << "[DEBUG] CPU limit set to " << cpu_cores << " cores (quota=" << quota << ", period=" << period << ")" << std::endl;
        return 0;
    }
    
    int add_process(pid_t pid) {
        if (!created) return -1;
        
        std::cout << "[DEBUG] Adding process " << pid << " to cgroup" << std::endl;
        
        std::string procs_file = cgroup_path + "/cgroup.procs";
        std::ofstream procs(procs_file);
        if (!procs.is_open()) {
            perror("Failed to add process to cgroup");
            return -1;
        }
        
        procs << pid;
        procs.close();
        
        return 0;
    }
    
    void cleanup() {
        if (!created) return;
        
        std::cout << "[DEBUG] Cleaning up cgroup: " << cgroup_path << std::endl;
        
        // Remove the cgroup directory
        if (rmdir(cgroup_path.c_str()) != 0) {
            // It's OK if this fails - the kernel will clean it up eventually
            std::cout << "[DEBUG] Note: cgroup cleanup will happen automatically" << std::endl;
        }
        
        created = false;
    }
    
private:
    long long parse_memory_limit(const std::string& limit) {
        if (limit.empty()) return -1;
        
        std::string num_str = limit;
        char unit = 'b';
        
        // Check for unit suffix
        if (!std::isdigit(limit.back())) {
            unit = std::tolower(limit.back());
            num_str = limit.substr(0, limit.length() - 1);
        }
        
        try {
            long long num = std::stoll(num_str);
            
            switch (unit) {
                case 'b': return num;
                case 'k': return num * 1024LL;
                case 'm': return num * 1024LL * 1024LL;
                case 'g': return num * 1024LL * 1024LL * 1024LL;
                default:
                    std::cerr << "Error: Unknown memory unit '" << unit << "'. Use b, k, m, or g." << std::endl;
                    return -1;
            }
        } catch (const std::exception& e) {
            std::cerr << "Error parsing memory limit: " << e.what() << std::endl;
            return -1;
        }
    }
};

// Helper function to copy a file
int copy_file(const std::string& src, const std::string& dst) {
    std::ifstream source(src, std::ios::binary);
    if (!source.is_open()) {
        return -1; // Source doesn't exist, skip silently
    }
    
    std::ofstream dest(dst, std::ios::binary);
    if (!dest.is_open()) {
        return -1;
    }
    
    dest << source.rdbuf();
    
    // Copy permissions
    struct stat src_stat;
    if (stat(src.c_str(), &src_stat) == 0) {
        chmod(dst.c_str(), src_stat.st_mode);
    }
    
    return 0;
}

// Helper function to copy shared libraries
int copy_shared_libraries(const std::string& binary, const std::string& rootfs) {
    std::string cmd = "ldd " + binary + " 2>/dev/null";
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return -1;
    
    char buffer[1024];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        std::string line(buffer);
        
        // Parse ldd output: "library => path (address)" or "path (address)"
        size_t arrow = line.find(" => ");
        size_t paren = line.find(" (0x");
        
        std::string lib_path;
        if (arrow != std::string::npos && paren != std::string::npos) {
            // Format: "library => path (address)"
            lib_path = line.substr(arrow + 4, paren - arrow - 4);
        } else if (paren != std::string::npos && line.find("/") != std::string::npos) {
            // Format: "path (address)" 
            lib_path = line.substr(0, paren);
        }
        
        // Trim whitespace
        lib_path.erase(0, lib_path.find_first_not_of(" \t"));
        lib_path.erase(lib_path.find_last_not_of(" \t\n\r") + 1);
        
        if (!lib_path.empty() && lib_path[0] == '/') {
            // Create directory structure
            std::string dst_path = rootfs + lib_path;
            std::string dst_dir = dst_path.substr(0, dst_path.find_last_of('/'));
            
            std::filesystem::create_directories(dst_dir);
            copy_file(lib_path, dst_path);
        }
    }
    
    pclose(pipe);
    return 0;
}

// Container setup functions (improved for Phase 2)
int setup_container_filesystem() {
    std::cout << "[DEBUG] Setting up container filesystem" << std::endl;
    
    const char* rootfs = "/tmp/iza-rootfs";
    
    // Remove and recreate rootfs to ensure clean state
    std::filesystem::remove_all(rootfs);
    
    // Create rootfs directory
    if (mkdir(rootfs, 0755) != 0) {
        perror("Failed to create rootfs directory");
        return -1;
    }
    
    // Create essential directories
    std::vector<std::string> dirs = {
        "/bin", "/usr", "/usr/bin", "/etc", "/proc", "/tmp", "/dev",
        "/lib", "/lib64", "/lib/x86_64-linux-gnu", "/usr/lib", "/usr/lib/x86_64-linux-gnu"
    };
    
    for (const auto& dir : dirs) {
        std::string full_path = std::string(rootfs) + dir;
        std::filesystem::create_directories(full_path);
    }
    
    // Copy essential binaries and their dependencies
    std::vector<std::pair<std::string, std::string>> binaries = {
        {"/bin/bash", "/bin/bash"},
        {"/bin/ls", "/bin/ls"},
        {"/bin/ps", "/bin/ps"},
        {"/usr/bin/whoami", "/usr/bin/whoami"},
        {"/bin/cat", "/bin/cat"},
        {"/usr/bin/stress", "/usr/bin/stress"},  // For testing resource limits
        {"/bin/sh", "/bin/sh"},                   // Fallback shell
        {"/bin/hostname", "/bin/hostname"},       // For hostname command
        {"/usr/bin/yes", "/usr/bin/yes"},        // For simple CPU stress testing
        {"/usr/bin/head", "/usr/bin/head"},      // For limiting output
        {"/bin/rm", "/bin/rm"},                  // For removing files
        {"/usr/bin/du", "/usr/bin/du"},          // For checking disk usage
        {"/bin/sleep", "/bin/sleep"},            // For delays
        {"/usr/bin/timeout", "/usr/bin/timeout"}, // For timing out commands
    };
    
    for (const auto& [src, dst] : binaries) {
        std::string dst_path = std::string(rootfs) + dst;
        
        if (copy_file(src, dst_path) == 0) {
            std::cout << "[DEBUG] Copied: " << src << std::endl;
            // Copy shared libraries for this binary
            copy_shared_libraries(src, rootfs);
        } else {
            std::cout << "[DEBUG] Skipped (not found): " << src << std::endl;
        }
    }
    
    // Copy dynamic linker/loader (essential for running binaries)
    std::vector<std::string> loaders = {
        "/lib64/ld-linux-x86-64.so.2",
        "/lib/ld-linux.so.2"
    };
    
    for (const auto& loader : loaders) {
        std::string dst_path = std::string(rootfs) + loader;
        std::string dst_dir = dst_path.substr(0, dst_path.find_last_of('/'));
        std::filesystem::create_directories(dst_dir);
        copy_file(loader, dst_path);
    }
    
    // Create basic /etc/hostname
    std::string hostname_file = std::string(rootfs) + "/etc/hostname";
    std::ofstream hostname(hostname_file);
    if (hostname.is_open()) {
        hostname << "iza-container" << std::endl;
        hostname.close();
    }
    
    // Create a basic /etc/passwd for whoami to work
    std::string passwd_file = std::string(rootfs) + "/etc/passwd";
    std::ofstream passwd(passwd_file);
    if (passwd.is_open()) {
        passwd << "root:x:0:0:root:/root:/bin/bash" << std::endl;
        passwd.close();
    }
    
    std::cout << "[DEBUG] Container filesystem setup complete" << std::endl;
    return 0;
}

int container_child(void* arg) {
    Arguments* args = static_cast<Arguments*>(arg);
    
    std::cout << "[CHILD] Container process starting (PID: " << getpid() << ")" << std::endl;
    
    // Set hostname
    if (sethostname("iza-container", 13) != 0) {
        perror("Failed to set hostname");
    }
    
    // Change root to our container filesystem
    const char* rootfs = "/tmp/iza-rootfs";
    if (chroot(rootfs) != 0) {
        perror("Failed to chroot");
        return -1;
    }
    
    // Change to root directory inside the container
    if (chdir("/") != 0) {
        perror("Failed to change directory to /");
        return -1;
    }
    
    // Mount /proc filesystem
    if (mount("proc", "/proc", "proc", 0, nullptr) != 0) {
        perror("Failed to mount /proc");
    }
    
    // Mount /tmp filesystem
    if (mount("tmpfs", "/tmp", "tmpfs", 0, nullptr) != 0) {
        perror("Failed to mount /tmp");
    }
    
    std::cout << "[CHILD] Container environment ready. Executing command..." << std::endl;
    
    // Prepare command arguments
    std::vector<char*> exec_args;
    for (const auto& cmd : args->command) {
        exec_args.push_back(const_cast<char*>(cmd.c_str()));
    }
    exec_args.push_back(nullptr);
    
    // Execute the user's command
    if (execv(exec_args[0], exec_args.data()) != 0) {
        perror("Failed to execute command");
        return -1;
    }
    
    return 0;
}

int main(int argc, char* argv[]) {
    std::cout << "🎯 Iza Container Runtime - Phase 2: Resource Control" << std::endl;
    std::cout << "=====================================================" << std::endl;
    
    // Parse command line arguments
    Arguments args;
    if (!args.parse(argc, argv)) {
        return 1;
    }
    
    std::cout << "[DEBUG] Command: ";
    for (const auto& cmd : args.command) {
        std::cout << cmd << " ";
    }
    std::cout << std::endl;
    
    if (!args.memory_limit.empty()) {
        std::cout << "[DEBUG] Memory limit: " << args.memory_limit << std::endl;
    }
    if (!args.cpu_limit.empty()) {
        std::cout << "[DEBUG] CPU limit: " << args.cpu_limit << std::endl;
    }
    
    // Set up container filesystem
    if (setup_container_filesystem() != 0) {
        std::cerr << "Failed to set up container filesystem" << std::endl;
        return 1;
    }
    
    // Create and configure cgroup (if limits specified)
    CgroupManager cgroup;
    bool use_cgroups = !args.memory_limit.empty() || !args.cpu_limit.empty();
    
    if (use_cgroups) {
        std::cout << "[DEBUG] Resource limits specified, setting up cgroup..." << std::endl;
        
        if (cgroup.create_cgroup() != 0) {
            std::cerr << "Failed to create cgroup" << std::endl;
            return 1;
        }
        
        if (!args.memory_limit.empty()) {
            if (cgroup.set_memory_limit(args.memory_limit) != 0) {
                std::cerr << "Failed to set memory limit" << std::endl;
                return 1;
            }
        }
        
        if (!args.cpu_limit.empty()) {
            if (cgroup.set_cpu_limit(args.cpu_limit) != 0) {
                std::cerr << "Failed to set CPU limit" << std::endl;
                return 1;
            }
        }
    }
    
    // Allocate stack for child process
    const size_t stack_size = 1024 * 1024; // 1MB stack
    void* stack = malloc(stack_size);
    if (!stack) {
        perror("Failed to allocate stack");
        return 1;
    }
    
    void* stack_top = static_cast<char*>(stack) + stack_size;
    
    // Clone flags for creating isolated namespaces
    int clone_flags = CLONE_NEWPID |    // New PID namespace
                     CLONE_NEWNS |      // New mount namespace  
                     CLONE_NEWUTS |     // New UTS namespace (hostname)
                     CLONE_NEWIPC |     // New IPC namespace
                     CLONE_NEWNET |     // New network namespace
                     SIGCHLD;           // Send SIGCHLD on termination
    
    std::cout << "[DEBUG] Creating container with clone()..." << std::endl;
    
    // Create the container process
    pid_t container_pid = clone(container_child, stack_top, clone_flags, &args);
    
    if (container_pid == -1) {
        perror("Failed to create container process");
        free(stack);
        return 1;
    }
    
    std::cout << "[PARENT] Container started with PID: " << container_pid << std::endl;
    
    // Add process to cgroup if we're using resource limits
    if (use_cgroups) {
        if (cgroup.add_process(container_pid) != 0) {
            std::cerr << "Warning: Failed to add process to cgroup" << std::endl;
        } else {
            std::cout << "[DEBUG] Process added to cgroup successfully" << std::endl;
        }
    }
    
    // Wait for container to finish
    int status;
    std::cout << "[PARENT] Waiting for container to finish..." << std::endl;
    
    if (waitpid(container_pid, &status, 0) == -1) {
        perror("Failed to wait for container");
        free(stack);
        return 1;
    }
    
    // Check exit status
    if (WIFEXITED(status)) {
        int exit_code = WEXITSTATUS(status);
        std::cout << "[PARENT] Container exited with code: " << exit_code << std::endl;
        free(stack);
        return exit_code;
    } else if (WIFSIGNALED(status)) {
        int signal = WTERMSIG(status);
        std::cout << "[PARENT] Container killed by signal: " << signal << std::endl;
        free(stack);
        return 128 + signal;
    }
    
    free(stack);
    return 0;
}