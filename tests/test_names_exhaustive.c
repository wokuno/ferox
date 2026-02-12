/**
 * test_names_exhaustive.c - Exhaustive name generation tests
 * Tests name format, validity, uniqueness, and edge cases
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "../src/shared/names.h"
#include "../src/shared/utils.h"

// Test framework
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { \
    printf("  Running %s... ", #name); \
    fflush(stdout); \
    test_##name(); \
    printf("PASSED\n"); \
    tests_passed++; \
} while(0)

#define ASSERT(cond, msg) do { \
    if (!(cond)) { \
        printf("FAILED\n    %s\n    At %s:%d\n", msg, __FILE__, __LINE__); \
        tests_failed++; \
        return; \
    } \
} while(0)

#define ASSERT_TRUE(cond) ASSERT(cond, #cond)
#define ASSERT_EQ(a, b) ASSERT((a) == (b), #a " == " #b)
#define ASSERT_NE(a, b) ASSERT((a) != (b), #a " != " #b)
#define ASSERT_LE(a, b) ASSERT((a) <= (b), #a " <= " #b)
#define ASSERT_GE(a, b) ASSERT((a) >= (b), #a " >= " #b)
#define ASSERT_GT(a, b) ASSERT((a) > (b), #a " > " #b)
#define ASSERT_NOT_NULL(ptr) ASSERT((ptr) != NULL, #ptr " is not NULL")

// Maximum name length we'll test with
#define MAX_NAME_LENGTH 64

// ============================================================================
// Helper Functions
// ============================================================================

// Check if character is valid for a scientific name (letters and space)
static int is_valid_name_char(char c) {
    return isalpha((unsigned char)c) || c == ' ';
}

// Check if name follows "Genus species" format
static int is_valid_scientific_format(const char* name) {
    if (!name || strlen(name) < 3) return 0;
    
    // Should have exactly one space separating genus and species
    const char* space = strchr(name, ' ');
    if (!space) return 0;
    
    // Shouldn't have multiple spaces
    if (strchr(space + 1, ' ') != NULL) return 0;
    
    // Genus (before space) should start with uppercase
    if (!isupper((unsigned char)name[0])) return 0;
    
    // Species (after space) should start with lowercase
    if (!islower((unsigned char)*(space + 1))) return 0;
    
    return 1;
}

// ============================================================================
// Format Tests
// ============================================================================

TEST(thousand_names_valid_format) {
    rng_seed(42);
    
    for (int i = 0; i < 1000; i++) {
        char name[MAX_NAME_LENGTH];
        generate_scientific_name(name, sizeof(name));
        
        ASSERT(strlen(name) > 0, "Name should not be empty");
        ASSERT(is_valid_scientific_format(name), "Name should be valid format");
    }
}

TEST(names_have_genus_and_species) {
    rng_seed(42);
    
    for (int i = 0; i < 100; i++) {
        char name[MAX_NAME_LENGTH];
        generate_scientific_name(name, sizeof(name));
        
        char* space = strchr(name, ' ');
        ASSERT_NOT_NULL(space);
        
        // Genus part
        size_t genus_len = space - name;
        ASSERT_GT(genus_len, 0);
        
        // Species part
        size_t species_len = strlen(space + 1);
        ASSERT_GT(species_len, 0);
    }
}

TEST(genus_starts_uppercase) {
    rng_seed(42);
    
    for (int i = 0; i < 100; i++) {
        char name[MAX_NAME_LENGTH];
        generate_scientific_name(name, sizeof(name));
        
        ASSERT(isupper((unsigned char)name[0]), "Genus should start with uppercase");
    }
}

TEST(species_starts_lowercase) {
    rng_seed(42);
    
    for (int i = 0; i < 100; i++) {
        char name[MAX_NAME_LENGTH];
        generate_scientific_name(name, sizeof(name));
        
        char* space = strchr(name, ' ');
        ASSERT_NOT_NULL(space);
        ASSERT(islower((unsigned char)*(space + 1)), "Species should start with lowercase");
    }
}

// ============================================================================
// Length Tests
// ============================================================================

TEST(names_dont_exceed_max_length) {
    rng_seed(42);
    
    for (int i = 0; i < 1000; i++) {
        char name[MAX_NAME_LENGTH];
        generate_scientific_name(name, sizeof(name));
        
        ASSERT(strlen(name) < MAX_NAME_LENGTH, "Name exceeds max length");
    }
}

TEST(names_have_minimum_length) {
    rng_seed(42);
    
    for (int i = 0; i < 1000; i++) {
        char name[MAX_NAME_LENGTH];
        generate_scientific_name(name, sizeof(name));
        
        // Minimum: at least "Ab cd" (5 chars)
        ASSERT_GE(strlen(name), 5);
    }
}

TEST(small_buffer_handling) {
    // Test with buffer too small
    char small[16];  // Minimum required is 16
    generate_scientific_name(small, sizeof(small));
    
    // Should either be empty or null-terminated properly
    ASSERT(small[sizeof(small) - 1] == '\0' || strlen(small) < sizeof(small),
           "Small buffer should be handled safely");
}

TEST(exact_minimum_buffer) {
    char buffer[16];
    generate_scientific_name(buffer, sizeof(buffer));
    
    // Should be null-terminated
    size_t len = 0;
    while (len < sizeof(buffer) && buffer[len] != '\0') len++;
    ASSERT(len < sizeof(buffer), "Name should be null-terminated");
}

// ============================================================================
// Character Validity Tests
// ============================================================================

TEST(names_contain_only_valid_chars) {
    rng_seed(42);
    
    for (int i = 0; i < 1000; i++) {
        char name[MAX_NAME_LENGTH];
        generate_scientific_name(name, sizeof(name));
        
        for (size_t j = 0; j < strlen(name); j++) {
            ASSERT(is_valid_name_char(name[j]), "Name contains invalid character");
        }
    }
}

TEST(no_numbers_in_names) {
    rng_seed(42);
    
    for (int i = 0; i < 1000; i++) {
        char name[MAX_NAME_LENGTH];
        generate_scientific_name(name, sizeof(name));
        
        for (size_t j = 0; j < strlen(name); j++) {
            ASSERT(!isdigit((unsigned char)name[j]), "Name contains number");
        }
    }
}

TEST(no_special_chars_in_names) {
    rng_seed(42);
    
    for (int i = 0; i < 1000; i++) {
        char name[MAX_NAME_LENGTH];
        generate_scientific_name(name, sizeof(name));
        
        for (size_t j = 0; j < strlen(name); j++) {
            char c = name[j];
            ASSERT(isalpha((unsigned char)c) || c == ' ',
                   "Name contains special character");
        }
    }
}

// ============================================================================
// Uniqueness Tests
// ============================================================================

TEST(names_have_variety) {
    rng_seed(42);
    
    #define NUM_SAMPLES 100
    char names[NUM_SAMPLES][MAX_NAME_LENGTH];
    
    for (int i = 0; i < NUM_SAMPLES; i++) {
        generate_scientific_name(names[i], MAX_NAME_LENGTH);
    }
    
    // Count unique names
    int unique = 0;
    for (int i = 0; i < NUM_SAMPLES; i++) {
        int is_unique = 1;
        for (int j = 0; j < i; j++) {
            if (strcmp(names[i], names[j]) == 0) {
                is_unique = 0;
                break;
            }
        }
        if (is_unique) unique++;
    }
    
    // At least 50% should be unique (with random generation, expect much higher)
    ASSERT_GE(unique, NUM_SAMPLES / 2);
    #undef NUM_SAMPLES
}

TEST(many_generations_uniqueness) {
    rng_seed(42);
    
    #define NUM_SAMPLES 500
    char names[NUM_SAMPLES][MAX_NAME_LENGTH];
    
    for (int i = 0; i < NUM_SAMPLES; i++) {
        generate_scientific_name(names[i], MAX_NAME_LENGTH);
    }
    
    // Count duplicates
    int duplicates = 0;
    for (int i = 0; i < NUM_SAMPLES; i++) {
        for (int j = i + 1; j < NUM_SAMPLES; j++) {
            if (strcmp(names[i], names[j]) == 0) {
                duplicates++;
            }
        }
    }
    
    // Allow some duplicates but not too many
    // With combinatorial generation, duplicates are possible but rare
    ASSERT(duplicates < NUM_SAMPLES / 10, "Too many duplicate names");
    #undef NUM_SAMPLES
}

// ============================================================================
// Consistency Tests
// ============================================================================

TEST(deterministic_with_seed) {
    rng_seed(12345);
    char name1[MAX_NAME_LENGTH];
    generate_scientific_name(name1, sizeof(name1));
    
    rng_seed(12345);
    char name2[MAX_NAME_LENGTH];
    generate_scientific_name(name2, sizeof(name2));
    
    ASSERT_EQ(strcmp(name1, name2), 0);
}

TEST(different_seeds_different_names) {
    rng_seed(12345);
    char name1[MAX_NAME_LENGTH];
    generate_scientific_name(name1, sizeof(name1));
    
    rng_seed(54321);
    char name2[MAX_NAME_LENGTH];
    generate_scientific_name(name2, sizeof(name2));
    
    // Very unlikely to be the same with different seeds
    ASSERT_NE(strcmp(name1, name2), 0);
}

// ============================================================================
// Format Consistency Tests
// ============================================================================

TEST(genus_species_format_consistent) {
    rng_seed(42);
    
    for (int i = 0; i < 100; i++) {
        char name[MAX_NAME_LENGTH];
        generate_scientific_name(name, sizeof(name));
        
        // Count spaces
        int spaces = 0;
        for (size_t j = 0; j < strlen(name); j++) {
            if (name[j] == ' ') spaces++;
        }
        
        // Should have exactly one space
        ASSERT_EQ(spaces, 1);
    }
}

TEST(no_leading_trailing_spaces) {
    rng_seed(42);
    
    for (int i = 0; i < 100; i++) {
        char name[MAX_NAME_LENGTH];
        generate_scientific_name(name, sizeof(name));
        
        size_t len = strlen(name);
        ASSERT_GT(len, 0);
        ASSERT(name[0] != ' ', "Leading space found");
        ASSERT(name[len - 1] != ' ', "Trailing space found");
    }
}

TEST(no_consecutive_spaces) {
    rng_seed(42);
    
    for (int i = 0; i < 100; i++) {
        char name[MAX_NAME_LENGTH];
        generate_scientific_name(name, sizeof(name));
        
        for (size_t j = 1; j < strlen(name); j++) {
            if (name[j] == ' ') {
                ASSERT(name[j-1] != ' ', "Consecutive spaces found");
            }
        }
    }
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST(null_buffer_handling) {
    // Should not crash
    generate_scientific_name(NULL, 64);
    ASSERT_TRUE(1);  // If we get here, we passed
}

TEST(zero_size_buffer) {
    char buffer[64] = "original";
    generate_scientific_name(buffer, 0);
    
    // Should not modify buffer with 0 size
    // Or might be empty - implementation dependent
    ASSERT_TRUE(1);  // If we get here without crash, passed
}

TEST(very_small_buffer) {
    char buffer[8] = {0};
    generate_scientific_name(buffer, sizeof(buffer));
    
    // Should either leave empty or be null-terminated
    if (buffer[0] != '\0') {
        ASSERT(buffer[sizeof(buffer) - 1] == '\0' || strlen(buffer) < sizeof(buffer),
               "Buffer should be safely null-terminated");
    }
}

// ============================================================================
// Run Tests
// ============================================================================

int run_names_exhaustive_tests(void) {
    tests_passed = 0;
    tests_failed = 0;
    
    printf("\n=== Name Generation Exhaustive Tests ===\n\n");
    
    printf("Format Tests:\n");
    RUN_TEST(thousand_names_valid_format);
    RUN_TEST(names_have_genus_and_species);
    RUN_TEST(genus_starts_uppercase);
    RUN_TEST(species_starts_lowercase);
    
    printf("\nLength Tests:\n");
    RUN_TEST(names_dont_exceed_max_length);
    RUN_TEST(names_have_minimum_length);
    RUN_TEST(small_buffer_handling);
    RUN_TEST(exact_minimum_buffer);
    
    printf("\nCharacter Validity Tests:\n");
    RUN_TEST(names_contain_only_valid_chars);
    RUN_TEST(no_numbers_in_names);
    RUN_TEST(no_special_chars_in_names);
    
    printf("\nUniqueness Tests:\n");
    RUN_TEST(names_have_variety);
    RUN_TEST(many_generations_uniqueness);
    
    printf("\nConsistency Tests:\n");
    RUN_TEST(deterministic_with_seed);
    RUN_TEST(different_seeds_different_names);
    
    printf("\nFormat Consistency Tests:\n");
    RUN_TEST(genus_species_format_consistent);
    RUN_TEST(no_leading_trailing_spaces);
    RUN_TEST(no_consecutive_spaces);
    
    printf("\nEdge Cases:\n");
    RUN_TEST(null_buffer_handling);
    RUN_TEST(zero_size_buffer);
    RUN_TEST(very_small_buffer);
    
    printf("\n--- Names Exhaustive Results ---\n");
    printf("Passed: %d\n", tests_passed);
    printf("Failed: %d\n", tests_failed);
    
    return tests_failed;
}

#ifdef STANDALONE_TEST
int main(void) {
    return run_names_exhaustive_tests() > 0 ? 1 : 0;
}
#endif
