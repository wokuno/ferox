add_library(ferox_server_lib STATIC
    atomic_sim.c
    genetics.c
    parallel.c
    server.c
    simulation.c
    simulation_common.c
    threadpool.c
    world.c
)

target_link_libraries(ferox_server_lib PUBLIC ferox_shared Threads::Threads)
target_include_directories(ferox_server_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

include(CheckCSourceCompiles)
string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" FEROX_SYSTEM_PROCESSOR)

if(FEROX_SYSTEM_PROCESSOR MATCHES "x86_64|amd64|x64")
    check_c_source_compiles("
        #include <immintrin.h>
        __attribute__((target(\"avx2\")))
        static void test_avx2(float* v) {
            __m256 x = _mm256_loadu_ps(v);
            _mm256_storeu_ps(v, x);
        }
        int main(void) {
            float data[8] = {0};
            test_avx2(data);
            return 0;
        }"
        FEROX_COMPILER_HAS_AVX2_TARGET)
    if(FEROX_COMPILER_HAS_AVX2_TARGET)
        target_compile_definitions(ferox_server_lib PRIVATE FEROX_SIMD_AVX2=1)
    endif()
elseif(FEROX_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
    check_c_source_compiles("
        #include <arm_neon.h>
        int main(void) {
            float data[4] = {0};
            float32x4_t x = vld1q_f32(data);
            vst1q_f32(data, x);
            return 0;
        }"
        FEROX_COMPILER_HAS_NEON)
    if(FEROX_COMPILER_HAS_NEON)
        target_compile_definitions(ferox_server_lib PRIVATE FEROX_SIMD_NEON=1)
    endif()
endif()

add_executable(ferox_server main.c)
target_link_libraries(ferox_server PRIVATE ferox_server_lib)
