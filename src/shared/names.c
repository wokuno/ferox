#include "names.h"
#include "utils.h"
#include <stdio.h>
#include <string.h>

// Genus prefixes (Latin-sounding)
static const char *genus_prefixes[] = {
    "Bacill", "Streptococ", "Staphylococ", "Clostridi",
    "Lactobacill", "Pseudomon", "Escheri", "Salmonel",
    "Vibri", "Spirochet", "Mycobacteri", "Actinomyc",
    "Rhizob", "Nitrosomon", "Thiobacill", "Cyanobacteri",
    "Thermophil", "Halophil", "Acidophil", "Methanogen",
    "Propionibacteri", "Bifidobacteri", "Corynebacteri", "Listero"
};
static const size_t genus_prefix_count = sizeof(genus_prefixes) / sizeof(genus_prefixes[0]);

// Genus suffixes
static const char *genus_suffixes[] = {
    "us", "um", "a", "ia", "ella", "oides", "ium", "ae"
};
static const size_t genus_suffix_count = sizeof(genus_suffixes) / sizeof(genus_suffixes[0]);

// Species prefixes
static const char *species_prefixes[] = {
    "ferox", "virid", "rub", "alb", "nigr", "aure", "ciner",
    "chlor", "cyan", "flav", "purpur", "ros", "viol",
    "mag", "parv", "grand", "long", "brev", "crass", "tenu",
    "veloc", "tard", "fort", "debil", "acer", "mild",
    "arctic", "tropic", "aquat", "terrest", "mar", "dulc"
};
static const size_t species_prefix_count = sizeof(species_prefixes) / sizeof(species_prefixes[0]);

// Species suffixes
static const char *species_suffixes[] = {
    "ii", "ens", "ans", "is", "us", "a", "icus", "alis",
    "inus", "atus", "ensis", "oides", "escens", "ifer"
};
static const size_t species_suffix_count = sizeof(species_suffixes) / sizeof(species_suffixes[0]);

void generate_scientific_name(char *buffer, size_t buffer_size) {
    if (buffer == NULL || buffer_size < 16) {
        return;
    }

    // Select random components
    const char *genus_pre = genus_prefixes[rand_int((int)genus_prefix_count)];
    const char *genus_suf = genus_suffixes[rand_int((int)genus_suffix_count)];
    const char *species_pre = species_prefixes[rand_int((int)species_prefix_count)];
    const char *species_suf = species_suffixes[rand_int((int)species_suffix_count)];

    // Combine into scientific name
    snprintf(buffer, buffer_size, "%s%s %s%s", 
             genus_pre, genus_suf, species_pre, species_suf);
}
