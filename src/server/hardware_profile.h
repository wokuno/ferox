#ifndef FEROX_HARDWARE_PROFILE_H
#define FEROX_HARDWARE_PROFILE_H

#include <stdbool.h>
#include <stdio.h>

typedef enum {
    FEROX_ACCELERATOR_PREFERENCE_AUTO = 0,
    FEROX_ACCELERATOR_PREFERENCE_CPU,
    FEROX_ACCELERATOR_PREFERENCE_APPLE,
    FEROX_ACCELERATOR_PREFERENCE_AMD
} FeroxAcceleratorPreference;

typedef enum {
    FEROX_ACCELERATOR_BACKEND_CPU = 0,
    FEROX_ACCELERATOR_BACKEND_APPLE,
    FEROX_ACCELERATOR_BACKEND_AMD
} FeroxAcceleratorBackend;

typedef struct {
    char os_name[32];
    char arch_name[32];
    char cpu_vendor[64];
    char gpu_vendor[64];
    int logical_cpus;
    bool has_gpu;
    bool has_compute_gpu;
    bool has_apple_gpu;
    bool has_amd_gpu;
    bool unified_memory;
} FeroxHardwareInfo;

typedef struct {
    FeroxAcceleratorPreference requested;
    FeroxAcceleratorBackend selected;
    int recommended_threads;
    const char* threadpool_profile;
    int atomic_serial_interval;
    int atomic_frontier_dense_pct;
    bool atomic_frontier_enabled;
    bool gpu_offload_enabled;
    const char* reason;
} FeroxRuntimeTuning;

void ferox_hardware_info_init(FeroxHardwareInfo* info);
int ferox_detect_hardware(FeroxHardwareInfo* info);

bool ferox_accelerator_preference_from_string(const char* raw, FeroxAcceleratorPreference* out);
const char* ferox_accelerator_preference_name(FeroxAcceleratorPreference preference);
const char* ferox_accelerator_backend_name(FeroxAcceleratorBackend backend);

void ferox_runtime_tuning_init(
    const FeroxHardwareInfo* info,
    FeroxAcceleratorPreference requested,
    FeroxRuntimeTuning* tuning
);

void ferox_apply_runtime_tuning_env(const FeroxRuntimeTuning* tuning);
void ferox_print_hardware_report(FILE* stream, const FeroxHardwareInfo* info, const FeroxRuntimeTuning* tuning);

#endif // FEROX_HARDWARE_PROFILE_H
