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
#include <curl/curl.h>
#include <archive.h>
#include <archive_entry.h>

class Arguments {
public:
    std::string command_type = "";      // "run", "pull", "images"
    std::string memory_limit = "";      // e.g., "100m", "1g"
    std::string cpu_limit = "";         // e.g., "1", "0.5"
    std::string image_name = "";        // e.g., "ubuntu:latest"
    std::vector<std::string> command;   // Command to run in container
    bool valid = false;
    
    bool parse(int argc, char* argv[]) {
        if (argc < 2) {
            show_usage();
            return false;
        }
        
        command_type = argv[1];
        
        if (command_type == "pull") {
            return parse_pull_command(argc, argv);
        } else if (command_type == "images") {
            return parse_images_command(argc, argv);
        } else if (command_type == "run") {
            return parse_run_command(argc, argv);
        } else {
            std::cerr << "Error: Unknown command '" << command_type << "'\n";
            show_usage();
            return false;
        }
    }
    
private:
    bool parse_pull_command(int argc, char* argv[]) {
        if (argc != 3) {
            std::cerr << "Usage: iza pull IMAGE\n";
            std::cerr << "Example: iza pull ubuntu:latest\n";
            return false;
        }
        image_name = argv[2];
        valid = true;
        return true;
    }
    
    bool parse_images_command(int argc, char* argv[]) {
        if (argc != 2) {
            std::cerr << "Usage: iza images\n";
            return false;
        }
        valid = true;
        return true;
    }
    
    bool parse_run_command(int argc, char* argv[]) {
        if (argc < 3) {
            std::cerr << "Usage: iza run [OPTIONS] IMAGE|COMMAND [ARGS...]\n";
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
                // Check if this looks like an image name (has : or is a known image)
                if (arg.find(':') != std::string::npos || is_available_image(arg)) {
                    image_name = arg;
                    i++;
                    // Rest are command arguments
                    while (i < argc) {
                        command.push_back(argv[i++]);
                    }
                } else {
                    // This is a direct command (old style)
                    while (i < argc) {
                        command.push_back(argv[i++]);
                    }
                }
                break;
            }
            i++;
        }
        
        if (command.empty() && !image_name.empty()) {
            // Default command for images
            command.push_back("/bin/bash");
        } else if (command.empty()) {
            std::cerr << "Error: No command specified\n";
            show_usage();
            return false;
        }
        
        valid = true;
        return true;
    }
    
    bool is_available_image(const std::string& name) {
        // Check if image exists locally
        std::string images_dir = "/var/lib/iza/images";
        std::string image_dir = images_dir + "/" + name;
        return std::filesystem::exists(image_dir + "/rootfs");
    }
    
    void show_usage() {
        std::cout << "🎯 Iza Container Runtime - Phase 3: Image Management\n\n"
                  << "Usage:\n"
                  << "  iza pull IMAGE                    Download a container image\n"
                  << "  iza images                        List downloaded images\n"
                  << "  iza run [OPTIONS] IMAGE [COMMAND] Run container from image\n"
                  << "  iza run [OPTIONS] COMMAND         Run container with custom rootfs\n\n"
                  << "Options:\n"
                  << "  --memory LIMIT    Memory limit (e.g., 100m, 1g)\n"
                  << "  --cpus LIMIT      CPU limit (e.g., 1, 0.5)\n\n"
                  << "Examples:\n"
                  << "  iza pull ubuntu:latest\n"
                  << "  iza images\n"
                  << "  iza run ubuntu:latest\n"
                  << "  iza run ubuntu:latest /bin/bash\n"
                  << "  iza run --memory 100m ubuntu:latest python3\n"
                  << "  iza run /bin/bash                 # Legacy mode\n";
    }
};

// Callback function for writing downloaded data
struct DownloadData {
    std::string data;
};

static size_t WriteCallback(void *contents, size_t size, size_t nmemb, DownloadData *userp) {
    size_t realsize = size * nmemb;
    userp->data.append(static_cast<char*>(contents), realsize);
    return realsize;
}

class ImageManager {
private:
    std::string images_dir = "/var/lib/iza/images";
    std::string cache_dir = "/var/lib/iza/cache";
    
public:
    ImageManager() {
        // Ensure directories exist
        std::filesystem::create_directories(images_dir);
        std::filesystem::create_directories(cache_dir);
    }
    
    int pull_image(const std::string& image_name) {
        std::cout << "[IMAGE] Pulling image: " << image_name << std::endl;
        
        // Parse image name (simple format: name:tag)
        std::string name, tag;
        size_t colon_pos = image_name.find(':');
        if (colon_pos != std::string::npos) {
            name = image_name.substr(0, colon_pos);
            tag = image_name.substr(colon_pos + 1);
        } else {
            name = image_name;
            tag = "latest";
        }
        
        // For now, we'll download a pre-built minimal rootfs
        // In a real implementation, this would query Docker Hub API
        std::string download_url;
        if (name == "ubuntu") {
            // Use a pre-built minimal Ubuntu rootfs
            download_url = "https://github.com/ianmackinnon/ubuntu-minimal-rootfs/releases/download/20.04/ubuntu-minimal-rootfs-20.04.tar.gz";
        } else if (name == "alpine") {
            // Use Alpine Linux minirootfs
            download_url = "https://dl-cdn.alpinelinux.org/alpine/v3.18/releases/x86_64/alpine-minirootfs-3.18.4-x86_64.tar.gz";
        } else {
            std::cerr << "Error: Unsupported image '" << name << "'. Supported: ubuntu, alpine" << std::endl;
            return -1;
        }
        
        // Download the image
        std::string image_path = cache_dir + "/" + image_name + ".tar.gz";
        if (download_file(download_url, image_path) != 0) {
            std::cerr << "Failed to download image" << std::endl;
            return -1;
        }
        
        // Extract the image
        std::string extract_dir = images_dir + "/" + image_name;
        if (extract_image(image_path, extract_dir) != 0) {
            std::cerr << "Failed to extract image" << std::endl;
            return -1;
        }
        
        std::cout << "[IMAGE] Successfully pulled " << image_name << std::endl;
        return 0;
    }
    
    int list_images() {
        std::cout << "REPOSITORY          TAG       SIZE\n";
        std::cout << "==========================================\n";
        
        if (!std::filesystem::exists(images_dir)) {
            std::cout << "(no images found)\n";
            return 0;
        }
        
        for (const auto& entry : std::filesystem::directory_iterator(images_dir)) {
            if (entry.is_directory()) {
                std::string image_name = entry.path().filename().string();
                std::string rootfs_path = entry.path() / "rootfs";
                
                if (std::filesystem::exists(rootfs_path)) {
                    // Calculate approximate size
                    size_t size = 0;
                    try {
                        for (const auto& file : std::filesystem::recursive_directory_iterator(rootfs_path)) {
                            if (file.is_regular_file()) {
                                size += std::filesystem::file_size(file);
                            }
                        }
                    } catch (const std::exception& e) {
                        size = 0;
                    }
                    
                    // Format size
                    std::string size_str;
                    if (size < 1024) {
                        size_str = std::to_string(size) + "B";
                    } else if (size < 1024 * 1024) {
                        size_str = std::to_string(size / 1024) + "KB";
                    } else {
                        size_str = std::to_string(size / (1024 * 1024)) + "MB";
                    }
                    
                    // Parse name:tag
                    size_t colon = image_name.find(':');
                    std::string repo, tag;
                    if (colon != std::string::npos) {
                        repo = image_name.substr(0, colon);
                        tag = image_name.substr(colon + 1);
                    } else {
                        repo = image_name;
                        tag = "latest";
                    }
                    
                    printf("%-20s %-9s %s\n", repo.c_str(), tag.c_str(), size_str.c_str());
                }
            }
        }
        
        return 0;
    }
    
    std::string get_image_rootfs(const std::string& image_name) {
        std::string image_dir = images_dir + "/" + image_name;
        std::string rootfs_dir = image_dir + "/rootfs";
        
        if (std::filesystem::exists(rootfs_dir)) {
            return rootfs_dir;
        }
        
        return "";
    }
    
private:
    int download_file(const std::string& url, const std::string& output_path) {
        std::cout << "[DOWNLOAD] Downloading from: " << url << std::endl;
        
        CURL *curl;
        CURLcode res;
        FILE *fp;
        
        curl = curl_easy_init();
        if (!curl) {
            std::cerr << "Failed to initialize curl" << std::endl;
            return -1;
        }
        
        fp = fopen(output_path.c_str(), "wb");
        if (!fp) {
            std::cerr << "Failed to create output file: " << output_path << std::endl;
            curl_easy_cleanup(curl);
            return -1;
        }
        
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "iza-container-runtime/1.0");
        
        res = curl_easy_perform(curl);
        
        fclose(fp);
        curl_easy_cleanup(curl);
        
        if (res != CURLE_OK) {
            std::cerr << "Download failed: " << curl_easy_strerror(res) << std::endl;
            std::filesystem::remove(output_path);
            return -1;
        }
        
        std::cout << "[DOWNLOAD] Downloaded to: " << output_path << std::endl;
        return 0;
    }
    
    int extract_image(const std::string& archive_path, const std::string& extract_dir) {
        std::cout << "[EXTRACT] Extracting to: " << extract_dir << std::endl;
        
        // Remove existing directory
        std::filesystem::remove_all(extract_dir);
        
        // Create directories
        std::filesystem::create_directories(extract_dir);
        std::string rootfs_dir = extract_dir + "/rootfs";
        std::filesystem::create_directories(rootfs_dir);
        
        struct archive *a;
        struct archive *ext;
        struct archive_entry *entry;
        int flags;
        int r;
        
        // Select which attributes we want to restore.
        flags = ARCHIVE_EXTRACT_TIME;
        flags |= ARCHIVE_EXTRACT_PERM;
        flags |= ARCHIVE_EXTRACT_ACL;
        flags |= ARCHIVE_EXTRACT_FFLAGS;
        
        a = archive_read_new();
        archive_read_support_format_all(a);
        archive_read_support_filter_all(a);
        ext = archive_write_disk_new();
        archive_write_disk_set_options(ext, flags);
        archive_write_disk_set_standard_lookup(ext);
        
        if ((r = archive_read_open_filename(a, archive_path.c_str(), 10240))) {
            std::cerr << "Failed to open archive: " << archive_error_string(a) << std::endl;
            archive_read_free(a);
            archive_write_free(ext);
            return -1;
        }
        
        for (;;) {
            r = archive_read_next_header(a, &entry);
            if (r == ARCHIVE_EOF)
                break;
            if (r < ARCHIVE_OK)
                std::cerr << archive_error_string(a) << std::endl;
            if (r < ARCHIVE_WARN) {
                archive_read_free(a);
                archive_write_free(ext);
                return -1;
            }
            
            // Modify the pathname to extract into our rootfs directory
            const char* current_file = archive_entry_pathname(entry);
            std::string new_path = rootfs_dir + "/" + current_file;
            archive_entry_set_pathname(entry, new_path.c_str());
            
            r = archive_write_header(ext, entry);
            if (r < ARCHIVE_OK)
                std::cerr << archive_error_string(ext) << std::endl;
            else if (archive_entry_size(entry) > 0) {
                r = copy_data(a, ext);
                if (r < ARCHIVE_OK)
                    std::cerr << archive_error_string(ext) << std::endl;
                if (r < ARCHIVE_WARN) {
                    archive_read_free(a);
                    archive_write_free(ext);
                    return -1;
                }
            }
            r = archive_write_finish_entry(ext);
            if (r < ARCHIVE_OK)
                std::cerr << archive_error_string(ext) << std::endl;
            if (r < ARCHIVE_WARN) {
                archive_read_free(a);
                archive_write_free(ext);
                return -1;
            }
        }
        
        archive_read_close(a);
        archive_read_free(a);
        archive_write_close(ext);
        archive_write_free(ext);
        
        std::cout << "[EXTRACT] Extraction complete" << std::endl;
        return 0;
    }
    
    static int copy_data(struct archive *ar, struct archive *aw) {
        int r;
        const void *buff;
        size_t size;
        la_int64_t offset;
        
        for (;;) {
            r = archive_read_data_block(ar, &buff, &size, &offset);
            if (r == ARCHIVE_EOF)
                return (ARCHIVE_OK);
            if (r < ARCHIVE_OK)
                return (r);
            r = archive_write_data_block(aw, buff, size, offset);
            if (r < ARCHIVE_OK) {
                std::cerr << archive_error_string(aw) << std::endl;
                return (r);
            }
        }
    }
};

class OverlayFS {
private:
    std::string overlay_dir = "/var/lib/iza/overlay";
    
public:
    OverlayFS() {
        std::filesystem::create_directories(overlay_dir);
    }
    
    // Add this method to your OverlayFS class as a fallback
    int setup_bind_mount_fallback(const std::string& image_rootfs, const std::string& container_id, std::string& container_rootfs) {
        std::cout << "[FALLBACK] Using bind mount instead of overlay..." << std::endl;
        
        // Create a copy of the rootfs for this container
        std::string container_overlay = overlay_dir + "/" + container_id;
        container_rootfs = container_overlay + "/rootfs";
        
        // Clean up any existing directory
        std::filesystem::remove_all(container_overlay);
        std::filesystem::create_directories(container_overlay);
        
        // Copy the entire image rootfs to our container directory
        std::cout << "[FALLBACK] Copying rootfs from " << image_rootfs << " to " << container_rootfs << std::endl;
        
        try {
            std::filesystem::copy(image_rootfs, container_rootfs, 
                                 std::filesystem::copy_options::recursive |
                                 std::filesystem::copy_options::copy_symlinks);
        } catch (const std::filesystem::filesystem_error& e) {
            std::cerr << "[ERROR] Failed to copy rootfs: " << e.what() << std::endl;
            return -1;
        }
        
        std::cout << "[FALLBACK] Bind mount fallback setup complete" << std::endl;
        return 0;
    }
    
    bool check_overlay_support() {
        // Check if overlay is supported in /proc/filesystems
        std::ifstream fs_file("/proc/filesystems");
        if (!fs_file.is_open()) {
            return false;
        }
        
        std::string line;
        while (std::getline(fs_file, line)) {
            if (line.find("overlay") != std::string::npos) {
                return true;
            }
        }
        return false;
    }
    
    bool validate_directories(const std::string& image_rootfs, const std::string& upper_dir, 
                            const std::string& work_dir, const std::string& merged_dir) {
        return std::filesystem::exists(image_rootfs) &&
               std::filesystem::exists(upper_dir) &&
               std::filesystem::exists(work_dir) &&
               std::filesystem::exists(merged_dir);
    }
    
    // Modified setup_overlay method with fallback
    int setup_overlay(const std::string& image_rootfs, const std::string& container_id, std::string& merged_dir) {
        // First try the original overlay approach
        if (check_overlay_support()) {
            std::string container_overlay = overlay_dir + "/" + container_id;
            std::string upper_dir = container_overlay + "/upper";
            std::string work_dir = container_overlay + "/work";
            merged_dir = container_overlay + "/merged";
            
            cleanup_overlay(container_id);
            
            try {
                std::filesystem::create_directories(upper_dir);
                std::filesystem::create_directories(work_dir);  
                std::filesystem::create_directories(merged_dir);
            } catch (const std::exception& e) {
                std::cerr << "[WARNING] Failed to create overlay dirs, falling back to bind mount" << std::endl;
                return setup_bind_mount_fallback(image_rootfs, container_id, merged_dir);
            }
            
            if (validate_directories(image_rootfs, upper_dir, work_dir, merged_dir)) {
                std::string mount_opts = "lowerdir=" + image_rootfs + 
                                       ",upperdir=" + upper_dir + 
                                       ",workdir=" + work_dir;
                
                if (mount("overlay", merged_dir.c_str(), "overlay", 0, mount_opts.c_str()) == 0) {
                    std::cout << "[OVERLAY] Successfully mounted overlay filesystem" << std::endl;
                    return 0;
                }
            }
        }
        
        // If overlay fails, fall back to bind mount
        std::cout << "[WARNING] OverlayFS not available, using bind mount fallback" << std::endl;
        return setup_bind_mount_fallback(image_rootfs, container_id, merged_dir);
    }
    
    int cleanup_overlay(const std::string& container_id) {
        std::string container_overlay = overlay_dir + "/" + container_id;
        std::string merged_dir = container_overlay + "/merged";
        
        // Unmount if mounted
        if (std::filesystem::exists(merged_dir)) {
            if (umount(merged_dir.c_str()) != 0) {
                // It's OK if this fails - might not be mounted
            }
        }
        
        // Remove the entire container overlay directory
        std::filesystem::remove_all(container_overlay);
        return 0;
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
        std::cout << "[CGROUP] Creating: " << cgroup_path << std::endl;
        
        // Check if cgroups v2 is available
        if (!std::filesystem::exists("/sys/fs/cgroup/cgroup.controllers")) {
            std::cerr << "Error: cgroups v2 not available" << std::endl;
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
        if (controllers.is_open()) {
            controllers << "+memory +cpu";
            controllers.close();
        }
        
        return 0;
    }
    
    int set_memory_limit(const std::string& limit) {
        if (!created) return -1;
        
        long long bytes = parse_memory_limit(limit);
        if (bytes <= 0) return -1;
        
        std::string memory_max_file = cgroup_path + "/memory.max";
        std::ofstream memory_file(memory_max_file);
        if (!memory_file.is_open()) {
            perror("Failed to set memory limit");
            return -1;
        }
        
        memory_file << bytes;
        memory_file.close();
        
        std::cout << "[CGROUP] Memory limit: " << limit << " (" << bytes << " bytes)" << std::endl;
        return 0;
    }
    
    int set_cpu_limit(const std::string& limit) {
        if (!created) return -1;
        
        double cpu_cores = std::stod(limit);
        if (cpu_cores <= 0) return -1;
        
        int period = 100000;
        int quota = (int)(cpu_cores * period);
        
        std::string cpu_max_file = cgroup_path + "/cpu.max";
        std::ofstream cpu_file(cpu_max_file);
        if (!cpu_file.is_open()) {
            perror("Failed to set CPU limit");
            return -1;
        }
        
        cpu_file << quota << " " << period;
        cpu_file.close();
        
        std::cout << "[CGROUP] CPU limit: " << limit << " cores" << std::endl;
        return 0;
    }
    
    int add_process(pid_t pid) {
        if (!created) return -1;
        
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
        
        if (rmdir(cgroup_path.c_str()) != 0) {
            // It's OK if this fails
        }
        
        created = false;
    }
    
private:
    long long parse_memory_limit(const std::string& limit) {
        if (limit.empty()) return -1;
        
        std::string num_str = limit;
        char unit = 'b';
        
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
                default: return -1;
            }
        } catch (const std::exception& e) {
            return -1;
        }
    }
};

// Legacy container filesystem setup (for backward compatibility)
int setup_legacy_filesystem() {
    std::cout << "[LEGACY] Setting up custom container filesystem" << std::endl;
    
    const char* rootfs = "/tmp/iza-rootfs";
    
    // Remove and recreate rootfs
    std::filesystem::remove_all(rootfs);
    
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
    
    // Copy essential binaries
    std::vector<std::pair<std::string, std::string>> binaries = {
        {"/bin/bash", "/bin/bash"},
        {"/bin/ls", "/bin/ls"},
        {"/bin/ps", "/bin/ps"},
        {"/usr/bin/whoami", "/usr/bin/whoami"},
        {"/bin/cat", "/bin/cat"},
        {"/usr/bin/stress", "/usr/bin/stress"},
        {"/bin/sh", "/bin/sh"},
        {"/bin/hostname", "/bin/hostname"}
    };
    
    for (const auto& [src, dst] : binaries) {
        std::string dst_path = std::string(rootfs) + dst;
        std::ifstream source(src, std::ios::binary);
        if (source.is_open()) {
            std::ofstream dest(dst_path, std::ios::binary);
            dest << source.rdbuf();
            chmod(dst_path.c_str(), 0755);
        }
    }
    
    // Create basic /etc files
    std::string hostname_file = std::string(rootfs) + "/etc/hostname";
    std::ofstream hostname(hostname_file);
    if (hostname.is_open()) {
        hostname << "iza-container" << std::endl;
        hostname.close();
    }
    
    return 0;
}

int container_child(void* arg) {
    Arguments* args = static_cast<Arguments*>(arg);
    
    std::cout << "[CHILD] Container process starting (PID: " << getpid() << ")" << std::endl;
    
    // Set hostname
    if (sethostname("iza-container", 13) != 0) {
        perror("Failed to set hostname");
    }
    
    // Determine rootfs path
    std::string rootfs_path;
    if (!args->image_name.empty()) {
        // Use image-based rootfs (will be set up by parent)
        rootfs_path = "/tmp/iza-container-" + std::to_string(getppid());
    } else {
        // Use legacy rootfs
        rootfs_path = "/tmp/iza-rootfs";
    }
    
    // Change root to our container filesystem
    if (chroot(rootfs_path.c_str()) != 0) {
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
    
    std::cout << "[CHILD] Container environment ready. Executing: ";
    for (const auto& cmd : args->command) {
        std::cout << cmd << " ";
    }
    std::cout << std::endl;
    
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
    std::cout << "🎯 Iza Container Runtime - Phase 3: Image Management" << std::endl;
    std::cout << "====================================================" << std::endl;
    
    // Initialize curl
    curl_global_init(CURL_GLOBAL_DEFAULT);
    
    // Parse command line arguments
    Arguments args;
    if (!args.parse(argc, argv)) {
        curl_global_cleanup();
        return 1;
    }
    
    ImageManager image_manager;
    
    // Handle different commands
    if (args.command_type == "pull") {
        int result = image_manager.pull_image(args.image_name);
        curl_global_cleanup();
        return result;
    } else if (args.command_type == "images") {
        int result = image_manager.list_images();
        curl_global_cleanup();
        return result;
    }
    
    // Handle "run" command
    std::cout << "[RUN] ";
    if (!args.image_name.empty()) {
        std::cout << "Image: " << args.image_name << " ";
    }
    std::cout << "Command: ";
    for (const auto& cmd : args.command) {
        std::cout << cmd << " ";
    }
    std::cout << std::endl;
    
    if (!args.memory_limit.empty()) {
        std::cout << "[RUN] Memory limit: " << args.memory_limit << std::endl;
    }
    if (!args.cpu_limit.empty()) {
        std::cout << "[RUN] CPU limit: " << args.cpu_limit << std::endl;
    }
    
    // Set up filesystem
    std::string container_rootfs;
    OverlayFS overlay;
    std::string container_id = "container-" + std::to_string(getpid()) + "-" + std::to_string(time(nullptr));
    
    if (!args.image_name.empty()) {
        // Use image-based container
        std::string image_rootfs = image_manager.get_image_rootfs(args.image_name);
        if (image_rootfs.empty()) {
            std::cerr << "Error: Image '" << args.image_name << "' not found. Try: iza pull " << args.image_name << std::endl;
            curl_global_cleanup();
            return 1;
        }
        
        std::cout << "[FILESYSTEM] Using image: " << args.image_name << std::endl;
        
        // Set up overlay filesystem
        if (overlay.setup_overlay(image_rootfs, container_id, container_rootfs) != 0) {
            std::cerr << "Failed to set up overlay filesystem" << std::endl;
            curl_global_cleanup();
            return 1;
        }
        
        // Create a symlink for the child process to find
        std::string child_rootfs = "/tmp/iza-container-" + std::to_string(getpid());
        std::filesystem::remove(child_rootfs); // Remove if exists
        if (symlink(container_rootfs.c_str(), child_rootfs.c_str()) != 0) {
            perror("Failed to create rootfs symlink");
            overlay.cleanup_overlay(container_id);
            curl_global_cleanup();
            return 1;
        }
        
    } else {
        // Use legacy custom filesystem
        if (setup_legacy_filesystem() != 0) {
            std::cerr << "Failed to set up legacy container filesystem" << std::endl;
            curl_global_cleanup();
            return 1;
        }
        container_rootfs = "/tmp/iza-rootfs";
    }
    
    // Create and configure cgroup (if limits specified)
    CgroupManager cgroup;
    bool use_cgroups = !args.memory_limit.empty() || !args.cpu_limit.empty();
    
    if (use_cgroups) {
        std::cout << "[CGROUP] Setting up resource limits..." << std::endl;
        
        if (cgroup.create_cgroup() != 0) {
            std::cerr << "Failed to create cgroup" << std::endl;
            if (!args.image_name.empty()) {
                overlay.cleanup_overlay(container_id);
                std::filesystem::remove("/tmp/iza-container-" + std::to_string(getpid()));
            }
            curl_global_cleanup();
            return 1;
        }
        
        if (!args.memory_limit.empty()) {
            if (cgroup.set_memory_limit(args.memory_limit) != 0) {
                std::cerr << "Failed to set memory limit" << std::endl;
                if (!args.image_name.empty()) {
                    overlay.cleanup_overlay(container_id);
                    std::filesystem::remove("/tmp/iza-container-" + std::to_string(getpid()));
                }
                curl_global_cleanup();
                return 1;
            }
        }
        
        if (!args.cpu_limit.empty()) {
            if (cgroup.set_cpu_limit(args.cpu_limit) != 0) {
                std::cerr << "Failed to set CPU limit" << std::endl;
                if (!args.image_name.empty()) {
                    overlay.cleanup_overlay(container_id);
                    std::filesystem::remove("/tmp/iza-container-" + std::to_string(getpid()));
                }
                curl_global_cleanup();
                return 1;
            }
        }
    }
    
    // Allocate stack for child process
    const size_t stack_size = 1024 * 1024; // 1MB stack
    void* stack = malloc(stack_size);
    if (!stack) {
        perror("Failed to allocate stack");
        if (!args.image_name.empty()) {
            overlay.cleanup_overlay(container_id);
            std::filesystem::remove("/tmp/iza-container-" + std::to_string(getpid()));
        }
        curl_global_cleanup();
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
    
    std::cout << "[CONTAINER] Creating container with clone()..." << std::endl;
    
    // Create the container process
    pid_t container_pid = clone(container_child, stack_top, clone_flags, &args);
    
    if (container_pid == -1) {
        perror("Failed to create container process");
        free(stack);
        if (!args.image_name.empty()) {
            overlay.cleanup_overlay(container_id);
            std::filesystem::remove("/tmp/iza-container-" + std::to_string(getpid()));
        }
        curl_global_cleanup();
        return 1;
    }
    
    std::cout << "[PARENT] Container started with PID: " << container_pid << std::endl;
    
    // Add process to cgroup if we're using resource limits
    if (use_cgroups) {
        if (cgroup.add_process(container_pid) != 0) {
            std::cerr << "Warning: Failed to add process to cgroup" << std::endl;
        } else {
            std::cout << "[CGROUP] Process added to cgroup successfully" << std::endl;
        }
    }
    
    // Wait for container to finish
    int status;
    std::cout << "[PARENT] Waiting for container to finish..." << std::endl;
    
    if (waitpid(container_pid, &status, 0) == -1) {
        perror("Failed to wait for container");
        free(stack);
        if (!args.image_name.empty()) {
            overlay.cleanup_overlay(container_id);
            std::filesystem::remove("/tmp/iza-container-" + std::to_string(getpid()));
        }
        curl_global_cleanup();
        return 1;
    }
    
    // Cleanup
    free(stack);
    
    if (!args.image_name.empty()) {
        std::cout << "[CLEANUP] Cleaning up overlay filesystem..." << std::endl;
        overlay.cleanup_overlay(container_id);
        std::filesystem::remove("/tmp/iza-container-" + std::to_string(getpid()));
    }
    
    curl_global_cleanup();
    
    // Check exit status
    if (WIFEXITED(status)) {
        int exit_code = WEXITSTATUS(status);
        std::cout << "[PARENT] Container exited with code: " << exit_code << std::endl;
        return exit_code;
    } else if (WIFSIGNALED(status)) {
        int signal = WTERMSIG(status);
        std::cout << "[PARENT] Container killed by signal: " << signal << std::endl;
        return 128 + signal;
    }
    
    return 0;
}