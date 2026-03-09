#include <stdio.h>
#include <string.h>

#include "../src/server/hardware_profile.h"

#define TEST_START(name) printf("  Testing %s... ", name)
#define TEST_PASS() printf("PASSED\n")
#define TEST_FAIL(msg) do { printf("FAILED: %s\n", msg); return 1; } while(0)

#define ASSERT_TRUE(cond) do { if (!(cond)) TEST_FAIL(#cond " is false"); } while(0)
#define ASSERT_FALSE(cond) do { if (cond) TEST_FAIL(#cond " is true"); } while(0)
#define ASSERT_EQ(a, b) do { if ((a) != (b)) TEST_FAIL(#a " != " #b); } while(0)
#define ASSERT_STREQ(a, b) do { if (strcmp((a), (b)) != 0) TEST_FAIL(#a " != " #b); } while(0)

static FeroxHardwareInfo make_info(int logical_cpus) {
    FeroxHardwareInfo info;
    ferox_hardware_info_init(&info);
    info.logical_cpus = logical_cpus;
    return info;
}

static int test_parse_accelerator_preference_accepts_expected_values(void) {
    TEST_START("accelerator parsing");

    FeroxAcceleratorPreference pref;
    ASSERT_TRUE(ferox_accelerator_preference_from_string("auto", &pref));
    ASSERT_EQ(pref, FEROX_ACCELERATOR_PREFERENCE_AUTO);
    ASSERT_TRUE(ferox_accelerator_preference_from_string("CPU", &pref));
    ASSERT_EQ(pref, FEROX_ACCELERATOR_PREFERENCE_CPU);
    ASSERT_TRUE(ferox_accelerator_preference_from_string("apple", &pref));
    ASSERT_EQ(pref, FEROX_ACCELERATOR_PREFERENCE_APPLE);
    ASSERT_TRUE(ferox_accelerator_preference_from_string("amd", &pref));
    ASSERT_EQ(pref, FEROX_ACCELERATOR_PREFERENCE_AMD);
    ASSERT_FALSE(ferox_accelerator_preference_from_string("bogus", &pref));

    TEST_PASS();
    return 0;
}

static int test_auto_selects_apple_target_when_available(void) {
    TEST_START("auto selects apple target");

    FeroxHardwareInfo info = make_info(10);
    info.has_gpu = true;
    info.has_compute_gpu = true;
    info.has_apple_gpu = true;
    info.unified_memory = true;

    FeroxRuntimeTuning tuning;
    ferox_runtime_tuning_init(&info, FEROX_ACCELERATOR_PREFERENCE_AUTO, &tuning);

    ASSERT_EQ(tuning.selected, FEROX_ACCELERATOR_BACKEND_APPLE);
    ASSERT_STREQ(tuning.threadpool_profile, "latency");
    ASSERT_EQ(tuning.atomic_serial_interval, 4);
    ASSERT_FALSE(tuning.gpu_offload_enabled);

    TEST_PASS();
    return 0;
}

static int test_auto_selects_amd_target_when_available(void) {
    TEST_START("auto selects amd target");

    FeroxHardwareInfo info = make_info(16);
    info.has_gpu = true;
    info.has_compute_gpu = true;
    info.has_amd_gpu = true;

    FeroxRuntimeTuning tuning;
    ferox_runtime_tuning_init(&info, FEROX_ACCELERATOR_PREFERENCE_AUTO, &tuning);

    ASSERT_EQ(tuning.selected, FEROX_ACCELERATOR_BACKEND_AMD);
    ASSERT_STREQ(tuning.threadpool_profile, "throughput");
    ASSERT_EQ(tuning.atomic_frontier_dense_pct, 12);

    TEST_PASS();
    return 0;
}

static int test_requested_amd_falls_back_to_cpu_when_unavailable(void) {
    TEST_START("amd fallback to cpu");

    FeroxHardwareInfo info = make_info(4);
    FeroxRuntimeTuning tuning;
    ferox_runtime_tuning_init(&info, FEROX_ACCELERATOR_PREFERENCE_AMD, &tuning);

    ASSERT_EQ(tuning.selected, FEROX_ACCELERATOR_BACKEND_CPU);
    ASSERT_STREQ(tuning.threadpool_profile, "latency");
    ASSERT_EQ(tuning.atomic_serial_interval, 5);

    TEST_PASS();
    return 0;
}

static int test_cpu_defaults_keep_at_least_one_thread(void) {
    TEST_START("cpu thread floor");

    FeroxHardwareInfo info = make_info(0);
    FeroxRuntimeTuning tuning;
    ferox_runtime_tuning_init(&info, FEROX_ACCELERATOR_PREFERENCE_CPU, &tuning);

    ASSERT_EQ(tuning.selected, FEROX_ACCELERATOR_BACKEND_CPU);
    ASSERT_EQ(tuning.recommended_threads, 1);

    TEST_PASS();
    return 0;
}

int main(void) {
    printf("Running hardware profile tests...\n\n");

    if (test_parse_accelerator_preference_accepts_expected_values() != 0) return 1;
    if (test_auto_selects_apple_target_when_available() != 0) return 1;
    if (test_auto_selects_amd_target_when_available() != 0) return 1;
    if (test_requested_amd_falls_back_to_cpu_when_unavailable() != 0) return 1;
    if (test_cpu_defaults_keep_at_least_one_thread() != 0) return 1;

    printf("\nAll hardware profile tests passed!\n");
    return 0;
}
