#include "hardware_profile.h"

#include <ctype.h>
#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/utsname.h>
#include <unistd.h>

static void copy_string(char* dst, size_t dst_size, const char* src) {
    if (!dst || dst_size == 0) {
        return;
    }

    if (!src) {
        dst[0] = '\0';
        return;
    }

    size_t len = strlen(src);
    if (len >= dst_size) {
        len = dst_size - 1;
    }

    memcpy(dst, src, len);
    dst[len] = '\0';
}

static void trim_trailing_whitespace(char* text) {
    if (!text) {
        return;
    }

    size_t len = strlen(text);
    while (len > 0 && isspace((unsigned char)text[len - 1])) {
        text[--len] = '\0';
    }
}

static void strip_key_prefix(char* text) {
    if (!text) {
        return;
    }

    char* colon = strchr(text, ':');
    if (!colon) {
        return;
    }

    colon++;
    while (*colon && isspace((unsigned char)*colon)) {
        colon++;
    }

    memmove(text, colon, strlen(colon) + 1);
}

static bool read_first_line(const char* path, char* buffer, size_t buffer_size) {
    if (!path || !buffer || buffer_size == 0) {
        return false;
    }

    FILE* fp = fopen(path, "r");
    if (!fp) {
        buffer[0] = '\0';
        return false;
    }

    if (!fgets(buffer, (int)buffer_size, fp)) {
        fclose(fp);
        buffer[0] = '\0';
        return false;
    }

    fclose(fp);
    trim_trailing_whitespace(buffer);
    return true;
}

static bool env_is_set(const char* name) {
    const char* raw = getenv(name);
    return raw && *raw;
}

static int clamp_thread_count(int logical_cpus) {
    if (logical_cpus < 1) {
        return 1;
    }
    if (logical_cpus > 64) {
        return 64;
    }
    return logical_cpus;
}

#if defined(__APPLE__)
static bool is_arm64_arch(const char* arch_name) {
    if (!arch_name || !*arch_name) {
        return false;
    }

    return strcmp(arch_name, "arm64") == 0 || strcmp(arch_name, "aarch64") == 0;
}
#endif

static const char* gpu_vendor_name(const char* vendor_id) {
    if (!vendor_id || !*vendor_id) {
        return "none";
    }
    if (strcmp(vendor_id, "0x1002") == 0) {
        return "AMD";
    }
    if (strcmp(vendor_id, "0x106b") == 0) {
        return "Apple";
    }
    if (strcmp(vendor_id, "0x10de") == 0) {
        return "NVIDIA";
    }
    if (strcmp(vendor_id, "0x8086") == 0) {
        return "Intel";
    }
    if (strcmp(vendor_id, "0x102b") == 0) {
        return "Matrox";
    }
    return "Unknown";
}

#if defined(__linux__)
static bool linux_has_amd_compute_runtime(void) {
    if (access("/dev/kfd", F_OK) == 0) {
        return true;
    }
    if (access("/opt/rocm", F_OK) == 0) {
        return true;
    }
    if (access("/etc/OpenCL/vendors", F_OK) == 0) {
        return true;
    }
    if (env_is_set("ROCM_PATH") || env_is_set("HIP_VISIBLE_DEVICES")) {
        return true;
    }
    return false;
}

static bool is_drm_card_entry(const char* name) {
    if (!name || strncmp(name, "card", 4) != 0) {
        return false;
    }

    const char* p = name + 4;
    if (!isdigit((unsigned char)*p)) {
        return false;
    }

    while (isdigit((unsigned char)*p)) {
        p++;
    }

    return *p == '\0';
}

static void detect_linux_cpu_vendor(FeroxHardwareInfo* info) {
    if (!info) {
        return;
    }

    FILE* fp = fopen("/proc/cpuinfo", "r");
    if (!fp) {
        return;
    }

    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "vendor_id", 9) == 0 || strncmp(line, "Hardware", 8) == 0 || strncmp(line, "model name", 10) == 0) {
            trim_trailing_whitespace(line);
            strip_key_prefix(line);
            copy_string(info->cpu_vendor, sizeof(info->cpu_vendor), line);
            break;
        }
    }

    fclose(fp);
}

static void detect_linux_gpu_vendor(FeroxHardwareInfo* info) {
    if (!info) {
        return;
    }

    DIR* dir = opendir("/sys/class/drm");
    if (!dir) {
        return;
    }

    struct dirent* entry = NULL;
    while ((entry = readdir(dir)) != NULL) {
        if (!is_drm_card_entry(entry->d_name)) {
            continue;
        }

        if (strlen(entry->d_name) > 200) {
            continue;
        }

        char path[256];
        char vendor_id[32];
        int written = snprintf(path, sizeof(path), "/sys/class/drm/%s/device/vendor", entry->d_name);
        if (written < 0 || (size_t)written >= sizeof(path)) {
            continue;
        }
        if (!read_first_line(path, vendor_id, sizeof(vendor_id))) {
            continue;
        }

        info->has_gpu = true;

        const char* vendor_name = gpu_vendor_name(vendor_id);
        if (!info->gpu_vendor[0] || strcmp(vendor_name, "AMD") == 0) {
            copy_string(info->gpu_vendor, sizeof(info->gpu_vendor), vendor_name);
        }

        if (strcmp(vendor_id, "0x1002") == 0) {
            info->has_amd_gpu = true;
            info->has_compute_gpu = linux_has_amd_compute_runtime();
        }
    }

    closedir(dir);
}
#endif

void ferox_hardware_info_init(FeroxHardwareInfo* info) {
    if (!info) {
        return;
    }

    memset(info, 0, sizeof(*info));
    copy_string(info->os_name, sizeof(info->os_name), "unknown");
    copy_string(info->arch_name, sizeof(info->arch_name), "unknown");
    copy_string(info->cpu_vendor, sizeof(info->cpu_vendor), "unknown");
    info->logical_cpus = 1;
}

int ferox_detect_hardware(FeroxHardwareInfo* info) {
    if (!info) {
        return -1;
    }

    ferox_hardware_info_init(info);

    struct utsname uts;
    if (uname(&uts) == 0) {
        copy_string(info->os_name, sizeof(info->os_name), uts.sysname);
        copy_string(info->arch_name, sizeof(info->arch_name), uts.machine);
    }

    long cpus = sysconf(_SC_NPROCESSORS_ONLN);
    if (cpus > 0) {
        info->logical_cpus = clamp_thread_count((int)cpus);
    }

#if defined(__APPLE__)
    bool arm64_host = is_arm64_arch(info->arch_name);
    if (arm64_host) {
        copy_string(info->cpu_vendor, sizeof(info->cpu_vendor), "Apple");
        copy_string(info->gpu_vendor, sizeof(info->gpu_vendor), "Apple");
        info->has_gpu = true;
        info->has_compute_gpu = true;
        info->has_apple_gpu = true;
        info->unified_memory = true;
        return 0;
    }
#endif

#if defined(__linux__)
    detect_linux_cpu_vendor(info);
    detect_linux_gpu_vendor(info);
#endif

    if (!info->has_gpu) {
        copy_string(info->gpu_vendor, sizeof(info->gpu_vendor), "none");
    }

    return 0;
}

bool ferox_accelerator_preference_from_string(const char* raw, FeroxAcceleratorPreference* out) {
    if (!raw || !out) {
        return false;
    }

    if (strcasecmp(raw, "auto") == 0) {
        *out = FEROX_ACCELERATOR_PREFERENCE_AUTO;
        return true;
    }
    if (strcasecmp(raw, "cpu") == 0) {
        *out = FEROX_ACCELERATOR_PREFERENCE_CPU;
        return true;
    }
    if (strcasecmp(raw, "apple") == 0) {
        *out = FEROX_ACCELERATOR_PREFERENCE_APPLE;
        return true;
    }
    if (strcasecmp(raw, "amd") == 0) {
        *out = FEROX_ACCELERATOR_PREFERENCE_AMD;
        return true;
    }

    return false;
}

const char* ferox_accelerator_preference_name(FeroxAcceleratorPreference preference) {
    switch (preference) {
        case FEROX_ACCELERATOR_PREFERENCE_CPU:
            return "cpu";
        case FEROX_ACCELERATOR_PREFERENCE_APPLE:
            return "apple";
        case FEROX_ACCELERATOR_PREFERENCE_AMD:
            return "amd";
        case FEROX_ACCELERATOR_PREFERENCE_AUTO:
        default:
            return "auto";
    }
}

const char* ferox_accelerator_backend_name(FeroxAcceleratorBackend backend) {
    switch (backend) {
        case FEROX_ACCELERATOR_BACKEND_APPLE:
            return "apple";
        case FEROX_ACCELERATOR_BACKEND_AMD:
            return "amd";
        case FEROX_ACCELERATOR_BACKEND_CPU:
        default:
            return "cpu";
    }
}

static void apply_cpu_tuning(const FeroxHardwareInfo* info, FeroxRuntimeTuning* tuning, const char* reason) {
    tuning->selected = FEROX_ACCELERATOR_BACKEND_CPU;
    tuning->threadpool_profile = (info && info->logical_cpus <= 4) ? "latency" : "balanced";
    tuning->atomic_serial_interval = 5;
    tuning->atomic_frontier_dense_pct = 15;
    tuning->atomic_frontier_enabled = true;
    tuning->gpu_offload_enabled = false;
    tuning->reason = reason;
}

static void apply_apple_tuning(FeroxRuntimeTuning* tuning, const char* reason) {
    tuning->selected = FEROX_ACCELERATOR_BACKEND_APPLE;
    tuning->threadpool_profile = "latency";
    tuning->atomic_serial_interval = 4;
    tuning->atomic_frontier_dense_pct = 18;
    tuning->atomic_frontier_enabled = true;
    tuning->gpu_offload_enabled = false;
    tuning->reason = reason;
}

static void apply_amd_tuning(FeroxRuntimeTuning* tuning, const char* reason) {
    tuning->selected = FEROX_ACCELERATOR_BACKEND_AMD;
    tuning->threadpool_profile = "throughput";
    tuning->atomic_serial_interval = 6;
    tuning->atomic_frontier_dense_pct = 12;
    tuning->atomic_frontier_enabled = true;
    tuning->gpu_offload_enabled = false;
    tuning->reason = reason;
}

void ferox_runtime_tuning_init(
    const FeroxHardwareInfo* info,
    FeroxAcceleratorPreference requested,
    FeroxRuntimeTuning* tuning
) {
    if (!tuning) {
        return;
    }

    memset(tuning, 0, sizeof(*tuning));
    tuning->requested = requested;
    tuning->recommended_threads = clamp_thread_count(info ? info->logical_cpus : 1);

    switch (requested) {
        case FEROX_ACCELERATOR_PREFERENCE_CPU:
            apply_cpu_tuning(info, tuning, "CPU target requested explicitly; using the lock-free atomic CPU execution path.");
            break;

        case FEROX_ACCELERATOR_PREFERENCE_APPLE:
            if (info && info->has_apple_gpu) {
                apply_apple_tuning(tuning, "Apple accelerator target selected; applying unified-memory friendly tuning on the current CPU execution path.");
            } else {
                apply_cpu_tuning(info, tuning, "Apple accelerator target requested, but no Apple GPU was detected on this host; falling back to CPU tuning.");
            }
            break;

        case FEROX_ACCELERATOR_PREFERENCE_AMD:
            if (info && info->has_amd_gpu) {
                apply_amd_tuning(tuning, "AMD accelerator target selected; applying throughput-oriented tuning on the current CPU execution path.");
            } else {
                apply_cpu_tuning(info, tuning, "AMD accelerator target requested, but no AMD GPU was detected on this host; falling back to CPU tuning.");
            }
            break;

        case FEROX_ACCELERATOR_PREFERENCE_AUTO:
        default:
            if (info && info->has_apple_gpu) {
                apply_apple_tuning(tuning, "Apple Silicon host detected; applying Apple-oriented runtime defaults.");
            } else if (info && info->has_amd_gpu) {
                apply_amd_tuning(tuning, "AMD GPU host detected; applying AMD-oriented runtime defaults.");
            } else {
                apply_cpu_tuning(info, tuning, "No Apple or AMD accelerator was detected; applying CPU-only runtime defaults.");
            }
            break;
    }
}

void ferox_apply_runtime_tuning_env(const FeroxRuntimeTuning* tuning) {
    if (!tuning) {
        return;
    }

    if (tuning->threadpool_profile && !env_is_set("FEROX_THREADPOOL_PROFILE")) {
        setenv("FEROX_THREADPOOL_PROFILE", tuning->threadpool_profile, 0);
    }

    if (!env_is_set("FEROX_ATOMIC_SERIAL_INTERVAL")) {
        char value[16];
        snprintf(value, sizeof(value), "%d", tuning->atomic_serial_interval);
        setenv("FEROX_ATOMIC_SERIAL_INTERVAL", value, 0);
    }

    if (!env_is_set("FEROX_ATOMIC_FRONTIER_DENSE_PCT")) {
        char value[16];
        snprintf(value, sizeof(value), "%d", tuning->atomic_frontier_dense_pct);
        setenv("FEROX_ATOMIC_FRONTIER_DENSE_PCT", value, 0);
    }

    if (!env_is_set("FEROX_ATOMIC_USE_FRONTIER")) {
        setenv("FEROX_ATOMIC_USE_FRONTIER", tuning->atomic_frontier_enabled ? "1" : "0", 0);
    }
}

void ferox_print_hardware_report(FILE* stream, const FeroxHardwareInfo* info, const FeroxRuntimeTuning* tuning) {
    if (!stream || !info || !tuning) {
        return;
    }

    fprintf(stream, "Hardware profile\n");
    fprintf(stream, "  OS: %s\n", info->os_name);
    fprintf(stream, "  Arch: %s\n", info->arch_name);
    fprintf(stream, "  CPU vendor: %s\n", info->cpu_vendor);
    fprintf(stream, "  Logical CPUs: %d\n", info->logical_cpus);
    fprintf(stream, "  GPU vendor: %s\n", info->gpu_vendor);
    fprintf(stream, "  Compute GPU available: %s\n", info->has_compute_gpu ? "yes" : "no");
    fprintf(stream, "  Unified memory: %s\n", info->unified_memory ? "yes" : "no");
    fprintf(stream, "Runtime tuning\n");
    fprintf(stream, "  Accelerator request: %s\n", ferox_accelerator_preference_name(tuning->requested));
    fprintf(stream, "  Selected target: %s\n", ferox_accelerator_backend_name(tuning->selected));
    fprintf(stream, "  GPU offload active: %s\n", tuning->gpu_offload_enabled ? "yes" : "no");
    fprintf(stream, "  Recommended threads: %d\n", tuning->recommended_threads);
    fprintf(stream, "  Threadpool profile default: %s\n", tuning->threadpool_profile ? tuning->threadpool_profile : "balanced");
    fprintf(stream, "  Atomic serial interval default: %d\n", tuning->atomic_serial_interval);
    fprintf(stream, "  Atomic frontier dense threshold default: %d%%\n", tuning->atomic_frontier_dense_pct);
    fprintf(stream, "  Note: explicit env vars still override these defaults.\n");
    fprintf(stream, "  Reason: %s\n", tuning->reason ? tuning->reason : "n/a");
}
