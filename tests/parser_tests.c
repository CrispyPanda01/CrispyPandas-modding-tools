#include "hoi4_map.h"
#include "character_creator.h"
#include "country_creator.h"
#include "path_utils.h"
#include "province_transfer.h"
#include "state_edit.h"

#include <windows.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static size_t occurrence_count(const char *text, const char *needle)
{
    size_t count = 0;
    size_t length = strlen(needle);
    while ((text = strstr(text, needle)) != NULL) {
        count++;
        text += length;
    }
    return count;
}

static bool write_test_bmp(const char *path)
{
    unsigned char bmp[70] = {
        'B','M',70,0,0,0, 0,0,0,0, 54,0,0,0,
        40,0,0,0, 2,0,0,0, 2,0,0,0, 1,0, 24,0,
        0,0,0,0, 16,0,0,0, 0,0,0,0, 0,0,0,0,
        0,0,0,0, 0,0,0,0,
        0,0,255, 0,255,0, 0,0,
        255,0,0, 255,255,255, 0,0
    };
    FILE *file = fopen(path, "wb");
    bool ok;
    if (!file) return false;
    ok = fwrite(bmp, 1, sizeof(bmp), file) == sizeof(bmp);
    return fclose(file) == 0 && ok;
}

static int test_state_edit(void)
{
    static const char input[] =
        "state={\r\n"
        "\tid=1\r\n"
        "\thistory={\r\n"
        "\t\towner = FRA\r\n"
        "\t\tadd_core_of = FRA\r\n"
        "\t\tadd_core_of = COR\r\n"
        "\t\t1939.1.1 = {\r\n"
        "\t\t\tadd_core_of = ENG\r\n"
        "\t\t}\r\n"
        "\t}\r\n"
        "\tprovinces={ 1 }\r\n"
        "}\r\n";
    StateEditRequest request;
    char error[512] = "";
    char *output = NULL;
    if (!state_edit_build_request(&request, "ger", "ita", "GER, POL ITA",
                                  false, error, sizeof(error))
        || !state_edit_transform(input, &request, &output, error, sizeof(error))) {
        fprintf(stderr, "Édition d'état impossible: %s\n", error);
        return 10;
    }
    if (occurrence_count(output, "owner = GER") != 1
        || occurrence_count(output, "controller = ITA") != 1
        || occurrence_count(output, "add_core_of = GER") != 1
        || occurrence_count(output, "add_core_of = POL") != 1
        || occurrence_count(output, "add_core_of = ITA") != 1
        || strstr(output, "add_core_of = FRA")
        || strstr(output, "add_core_of = COR")
        || strstr(output, "add_core_of = ENG")) {
        fprintf(stderr, "Résultat de remplacement incorrect:\n%s\n", output);
        free(output);
        return 11;
    }
    free(output);
    output = NULL;
    if (!state_edit_build_request(&request, "", "", "GER", true, error, sizeof(error))
        || !state_edit_transform(input, &request, &output, error, sizeof(error))) {
        fprintf(stderr, "Édition avec conservation impossible: %s\n", error);
        return 12;
    }
    if (!strstr(output, "add_core_of = GER")
        || !strstr(output, "add_core_of = FRA")
        || !strstr(output, "add_core_of = COR")
        || !strstr(output, "add_core_of = ENG")) {
        fprintf(stderr, "Les cores existants n'ont pas été conservés:\n%s\n", output);
        free(output);
        return 13;
    }
    free(output);
    output = NULL;
    if (!state_edit_build_request(&request, "", "POL", "", false, error, sizeof(error))
        || !state_edit_transform(input, &request, &output, error, sizeof(error))) {
        fprintf(stderr, "Édition controller-only impossible: %s\n", error);
        return 14;
    }
    if (!strstr(output, "owner = FRA")
        || occurrence_count(output, "add_core_of") != 3
        || occurrence_count(output, "controller = POL") != 1) {
        fprintf(stderr, "Un champ vide a modifié des données existantes:\n%s\n", output);
        free(output);
        return 15;
    }
    free(output);
    return 0;
}

static bool write_fixture(const char *path, const char *text)
{
    FILE *file = fopen(path, "wb");
    size_t length = strlen(text);
    if (!file) return false;
    if (fwrite(text, 1, length, file) != length) {
        fclose(file);
        return false;
    }
    return fclose(file) == 0;
}

static char *read_fixture(const char *path)
{
    FILE *file = fopen(path, "rb");
    long size;
    char *text;
    if (!file) return NULL;
    fseek(file, 0, SEEK_END); size = ftell(file); rewind(file);
    text = size >= 0 ? malloc((size_t)size + 1) : NULL;
    if (!text || fread(text, 1, (size_t)size, file) != (size_t)size) {
        free(text); fclose(file); return NULL;
    }
    fclose(file); text[size] = '\0'; return text;
}

static int test_province_transfer(void)
{
    static const char source_text[] =
        "state={\r\n"
        "\tid=1\r\n"
        "\thistory={\r\n"
        "\t\towner = AAA\r\n"
        "\t\tvictory_points = { 10 5 } # Source VP\r\n"
        "\t\tbuildings={\r\n"
        "\t\t\tinfrastructure=1\r\n"
        "\t\t\t10 = { # Port provincial\r\n"
        "\t\t\t\tnaval_base=2 # À conserver\r\n"
        "\t\t\t}\r\n"
        "\t\t}\r\n"
        "\t}\r\n"
        "\tprovinces={ 10 11 # Liste source\r\n"
        "\t}\r\n"
        "}\r\n";
    static const char target_text[] =
        "state={\r\n"
        "\tid=2\r\n"
        "\thistory={\r\n"
        "\t\towner = BBB\r\n"
        "\t\tbuildings={\r\n"
        "\t\t\tinfrastructure=2\r\n"
        "\t\t}\r\n"
        "\t}\r\n"
        "\tprovinces={ 20 21 }\r\n"
        "}\r\n";
    static const char region1_text[] =
        "strategic_region={ id=1 provinces={ 10 11 # Région source\r\n } }\r\n";
    static const char region2_text[] =
        "strategic_region={ id=2 provinces={ 20 21 } }\r\n";
    char cwd[CP_PATH_MAX] = "", root[CP_PATH_MAX] = "", base[CP_PATH_MAX] = "", mod[CP_PATH_MAX] = "";
    char source_path[CP_PATH_MAX] = "", target_path[CP_PATH_MAX] = "";
    char region1_path[CP_PATH_MAX] = "", region2_path[CP_PATH_MAX] = "";
    char mod_states[CP_PATH_MAX] = "", mod_regions[CP_PATH_MAX] = "";
    char out_source[CP_PATH_MAX] = "", out_target[CP_PATH_MAX] = "";
    char out_region1[CP_PATH_MAX] = "", out_region2[CP_PATH_MAX] = "";
    char out_created[CP_PATH_MAX] = "", out_localization[CP_PATH_MAX] = "";
    char error[512] = "";
    Hoi4Map map;
    Hoi4State *state1 = NULL, *state2 = NULL;
    Hoi4StrategicRegion *region1 = NULL, *region2 = NULL;
    uint8_t *selected = NULL;
    ProvinceTransferResult result;
    ProvinceStateCreateResult create_result;
    char *source_result = NULL, *target_result = NULL, *region1_result = NULL, *region2_result = NULL;
    char *created_result = NULL, *localization_result = NULL;
    int failure = 0;
    DWORD pid = GetCurrentProcessId();

    GetCurrentDirectoryA(sizeof(cwd), cwd);
    snprintf(root, sizeof(root), "%.4000s\\transfer-fixture-%lu", cwd, (unsigned long)pid);
    cp_path_join(base, sizeof(base), root, "base");
    cp_path_join(mod, sizeof(mod), root, "mod");
    CreateDirectoryA(root, NULL); CreateDirectoryA(base, NULL); CreateDirectoryA(mod, NULL);
    cp_path_join(source_path, sizeof(source_path), base, "1-source.txt");
    cp_path_join(target_path, sizeof(target_path), base, "2-target.txt");
    cp_path_join(region1_path, sizeof(region1_path), base, "1-region.txt");
    cp_path_join(region2_path, sizeof(region2_path), base, "2-region.txt");
    if (!write_fixture(source_path, source_text) || !write_fixture(target_path, target_text)
        || !write_fixture(region1_path, region1_text) || !write_fixture(region2_path, region2_text)) {
        failure = 20; goto cleanup;
    }
    hoi4_map_init(&map);
    map.states = calloc(HOI4_MAX_STATES, sizeof(*map.states));
    map.strategic_regions = calloc(HOI4_MAX_STATES, sizeof(*map.strategic_regions));
    map.provinces = calloc(HOI4_MAX_PROVINCES, sizeof(*map.provinces));
    state1 = calloc(1, sizeof(*state1)); state2 = calloc(1, sizeof(*state2));
    region1 = calloc(1, sizeof(*region1)); region2 = calloc(1, sizeof(*region2));
    selected = calloc(HOI4_MAX_PROVINCES, 1);
    if (!map.states || !map.strategic_regions || !map.provinces
        || !state1 || !state2 || !region1 || !region2 || !selected) {
        failure = 21; goto cleanup_map;
    }
    state1->id = 1; state1->province_count = 2; state1->provinces = malloc(2 * sizeof(int));
    state1->provinces[0] = 10; state1->provinces[1] = 11;
    snprintf(state1->owner, sizeof(state1->owner), "AAA");
    snprintf(state1->source, sizeof(state1->source), "%s", source_path);
    state2->id = 2; state2->province_count = 2; state2->provinces = malloc(2 * sizeof(int));
    state2->provinces[0] = 20; state2->provinces[1] = 21;
    snprintf(state2->owner, sizeof(state2->owner), "BBB");
    snprintf(state2->source, sizeof(state2->source), "%s", target_path);
    region1->id = 1; region1->province_count = 2; region1->provinces = malloc(2 * sizeof(int));
    region1->provinces[0] = 10; region1->provinces[1] = 11;
    snprintf(region1->source, sizeof(region1->source), "%s", region1_path);
    region2->id = 2; region2->province_count = 2; region2->provinces = malloc(2 * sizeof(int));
    region2->provinces[0] = 20; region2->provinces[1] = 21;
    snprintf(region2->source, sizeof(region2->source), "%s", region2_path);
    map.states[1] = state1; map.states[2] = state2;
    map.strategic_regions[1] = region1; map.strategic_regions[2] = region2;
    map.provinces[10] = (Hoi4Province){.id=10,.type=HOI4_PROVINCE_LAND,.state_id=1,.strategic_region_id=1};
    map.provinces[11] = (Hoi4Province){.id=11,.type=HOI4_PROVINCE_LAND,.state_id=1,.strategic_region_id=1};
    map.provinces[20] = (Hoi4Province){.id=20,.type=HOI4_PROVINCE_LAND,.state_id=2,.strategic_region_id=2};
    map.provinces[21] = (Hoi4Province){.id=21,.type=HOI4_PROVINCE_LAND,.state_id=2,.strategic_region_id=2};
    selected[10] = 1;
    selected[11] = 1;
    if (province_state_create_execute(&map, mod, selected, &create_result)
        || !strstr(create_result.error, "viderait complètement")) {
        fprintf(stderr, "Une création vidant l'état source n'a pas été refusée: %s\n",
                create_result.error);
        failure = 30; goto cleanup_map;
    }
    if (province_transfer_execute(&map, mod, selected, 2, &result)
        || !strstr(result.error, "viderait complètement")) {
        fprintf(stderr, "Un transfert vidant l'état source n'a pas été refusé: %s\n", result.error);
        failure = 24; goto cleanup_map;
    }
    selected[11] = 0;
    selected[20] = 1; /* Déjà dans la cible: doit être ignorée sans erreur. */
    if (!province_transfer_execute(&map, mod, selected, 2, &result)) {
        fprintf(stderr, "Transfert fixture impossible: %s\n", result.error);
        failure = 22; goto cleanup_map;
    }
    cp_path_join(mod_states, sizeof(mod_states), mod, "history\\states");
    cp_path_join(mod_regions, sizeof(mod_regions), mod, "map\\strategicregions");
    cp_path_join(out_source, sizeof(out_source), mod_states, "1-source.txt");
    cp_path_join(out_target, sizeof(out_target), mod_states, "2-target.txt");
    cp_path_join(out_region1, sizeof(out_region1), mod_regions, "1-region.txt");
    cp_path_join(out_region2, sizeof(out_region2), mod_regions, "2-region.txt");
    source_result = read_fixture(out_source); target_result = read_fixture(out_target);
    region1_result = read_fixture(out_region1); region2_result = read_fixture(out_region2);
    if (result.transferred_provinces != 1
        || !source_result || !target_result || !region1_result || !region2_result
        || strstr(source_result, "victory_points")
        || strstr(source_result, "naval_base")
        || !strstr(target_result, "victory_points = { 10 5 }")
        || !strstr(target_result, "10 = {")
        || !strstr(target_result, "# Source VP")
        || !strstr(target_result, "# Port provincial")
        || !strstr(target_result, "# À conserver")
        || !province_transfer_validate_syntax(source_result, error, sizeof(error))
        || !province_transfer_validate_syntax(target_result, error, sizeof(error))
        || !strstr(region1_result, "11")
        || strstr(region1_result, "10")
        || !strstr(region2_result, "10")
        || !strstr(region2_result, "20")) {
        fprintf(stderr, "Résultat du transfert incorrect: %s\nSOURCE:\n%s\nTARGET:\n%s\nR1:\n%s\nR2:\n%s\n",
                error, source_result ? source_result : "(null)", target_result ? target_result : "(null)",
                region1_result ? region1_result : "(null)", region2_result ? region2_result : "(null)");
        failure = 23;
    }
    if (!failure) {
        int *grown;
        /* Représente le monde après le premier transfert, puis renvoie la
           même province vers l'état 1. Les fichiers générés deviennent ainsi
           les sources du deuxième transfert. */
        state1->province_count = 1;
        state1->provinces[0] = 11;
        grown = realloc(state2->provinces, 3 * sizeof(int));
        if (!grown) { failure = 25; goto cleanup_map; }
        state2->provinces = grown;
        state2->province_count = 3;
        state2->provinces[0] = 10; state2->provinces[1] = 20; state2->provinces[2] = 21;
        snprintf(state1->source, sizeof(state1->source), "%s", out_source);
        snprintf(state2->source, sizeof(state2->source), "%s", out_target);
        region1->province_count = 1;
        region1->provinces[0] = 11;
        grown = realloc(region2->provinces, 3 * sizeof(int));
        if (!grown) { failure = 25; goto cleanup_map; }
        region2->provinces = grown;
        region2->province_count = 3;
        region2->provinces[0] = 10; region2->provinces[1] = 20; region2->provinces[2] = 21;
        snprintf(region1->source, sizeof(region1->source), "%s", out_region1);
        snprintf(region2->source, sizeof(region2->source), "%s", out_region2);
        map.provinces[10].state_id = 2;
        map.provinces[10].strategic_region_id = 2;
        selected[10] = 1; selected[20] = 0;
        if (!province_transfer_execute(&map, mod, selected, 1, &result)) {
            fprintf(stderr, "Deuxième transfert impossible: %s\n", result.error);
            failure = 26; goto cleanup_map;
        }
        free(source_result); free(target_result); free(region1_result); free(region2_result);
        source_result = read_fixture(out_source); target_result = read_fixture(out_target);
        region1_result = read_fixture(out_region1); region2_result = read_fixture(out_region2);
        if (!source_result || !target_result || !region1_result || !region2_result
            || !strstr(source_result, "victory_points = { 10 5 }")
            || !strstr(source_result, "10 = {")
            || strstr(target_result, "victory_points = { 10 5 }")
            || strstr(target_result, "10 = {")
            || !strstr(region1_result, "10")
            || strstr(region2_result, "10")) {
            fprintf(stderr, "Résultat du deuxième transfert incorrect.\n");
            failure = 27;
        }
    }
    if (!failure) {
        char mod_localization[CP_PATH_MAX];
        state1->province_count = 2;
        state1->provinces[0] = 10; state1->provinces[1] = 11;
        state2->province_count = 2;
        state2->provinces[0] = 20; state2->provinces[1] = 21;
        region1->province_count = 2;
        region1->provinces[0] = 10; region1->provinces[1] = 11;
        region2->province_count = 2;
        region2->provinces[0] = 20; region2->provinces[1] = 21;
        map.provinces[10].state_id = 1;
        map.provinces[10].strategic_region_id = 1;
        selected[10] = 1;
        selected[20] = 1;
        if (!province_state_create_execute_named(&map, mod, selected,
                                                 "Nouvel \"État\"",
                                                 &create_result)) {
            fprintf(stderr, "Création d'état impossible: %s\n", create_result.error);
            failure = 28; goto cleanup_map;
        }
        cp_path_join(out_created, sizeof(out_created), mod_states, "3-STATE_3.txt");
        cp_path_join(mod_localization, sizeof(mod_localization), mod, "localisation\\english");
        cp_path_join(out_localization, sizeof(out_localization), mod_localization,
                     "crispy_pandas_states_l_english.yml");
        free(source_result); free(target_result); free(region1_result); free(region2_result);
        source_result = read_fixture(out_source); target_result = read_fixture(out_target);
        region1_result = read_fixture(out_region1); region2_result = read_fixture(out_region2);
        created_result = read_fixture(out_created);
        localization_result = read_fixture(out_localization);
        if (create_result.state_id != 3
            || create_result.province_count != 2
            || strcmp(create_result.localization_key, "STATE_3") != 0
            || !source_result || !target_result || !created_result
            || !region1_result || !region2_result || !localization_result
            || strstr(source_result, "victory_points")
            || strstr(source_result, "10 = {")
            || !strstr(created_result, "id = 3")
            || !strstr(created_result, "name = \"STATE_3\"")
            || !strstr(created_result, "owner = AAA")
            || !strstr(created_result, "victory_points = { 10 5 }")
            || !strstr(created_result, "10 = {")
            || !strstr(created_result, " 10")
            || !strstr(created_result, " 20")
            || !strstr(source_result, "11")
            || !strstr(target_result, "21")
            || !strstr(region1_result, "20")
            || strstr(region2_result, "20")
            || !strstr(localization_result, "l_english:")
            || !strstr(localization_result,
                       "STATE_3:0 \"Nouvel \\\"État\\\"\"")
            || !province_transfer_validate_syntax(created_result, error, sizeof(error))) {
            fprintf(stderr, "Résultat de création incorrect: %s\nNEW:\n%s\nLOC:\n%s\n",
                    error, created_result ? created_result : "(null)",
                    localization_result ? localization_result : "(null)");
            failure = 29;
        }
    }

cleanup_map:
    free(selected);
    hoi4_map_free(&map);
cleanup:
    free(source_result); free(target_result); free(region1_result); free(region2_result);
    free(created_result); free(localization_result);
    DeleteFileA(out_created); DeleteFileA(out_localization);
    DeleteFileA(out_source); DeleteFileA(out_target); DeleteFileA(out_region1); DeleteFileA(out_region2);
    RemoveDirectoryA(mod_states); RemoveDirectoryA(mod_regions);
    {
        char history[CP_PATH_MAX], map_dir[CP_PATH_MAX];
        char localization[CP_PATH_MAX], english[CP_PATH_MAX];
        cp_path_join(history, sizeof(history), mod, "history");
        cp_path_join(map_dir, sizeof(map_dir), mod, "map");
        cp_path_join(localization, sizeof(localization), mod, "localisation");
        cp_path_join(english, sizeof(english), localization, "english");
        RemoveDirectoryA(history); RemoveDirectoryA(map_dir);
        RemoveDirectoryA(english); RemoveDirectoryA(localization);
    }
    DeleteFileA(source_path); DeleteFileA(target_path); DeleteFileA(region1_path); DeleteFileA(region2_path);
    RemoveDirectoryA(base); RemoveDirectoryA(mod); RemoveDirectoryA(root);
    return failure;
}

static bool dds_dimensions(const char *path, int *width, int *height)
{
    unsigned char header[20];
    FILE *file = fopen(path, "rb");
    if (!file) return false;
    if (fread(header, 1, sizeof(header), file) != sizeof(header)) {
        fclose(file);
        return false;
    }
    fclose(file);
    if (memcmp(header, "DDS ", 4) != 0) return false;
    *height = (int)(header[12] | header[13] << 8
                    | header[14] << 16 | header[15] << 24);
    *width = (int)(header[16] | header[17] << 8
                   | header[18] << 16 | header[19] << 24);
    return true;
}

static bool dds_pixel(const char *path, int x, int y,
                      unsigned char *red, unsigned char *green,
                      unsigned char *blue, unsigned char *alpha)
{
    unsigned char pixel[4];
    int width, height;
    FILE *file = NULL;
    if (!dds_dimensions(path, &width, &height)
        || x < 0 || y < 0 || x >= width || y >= height
        || !(file = fopen(path, "rb"))
        || fseek(file, 128L + ((long)y * width + x) * 4L, SEEK_SET) != 0
        || fread(pixel, 1, 4, file) != 4) {
        if (file) fclose(file);
        return false;
    }
    fclose(file);
    *blue = pixel[0];
    *green = pixel[1];
    *red = pixel[2];
    *alpha = pixel[3];
    return true;
}

static int test_character_creator(void)
{
    char cwd[CP_PATH_MAX] = "", root[CP_PATH_MAX] = "";
    char game[CP_PATH_MAX] = "", mod[CP_PATH_MAX] = "";
    char common[CP_PATH_MAX] = "", country_traits_dir[CP_PATH_MAX] = "";
    char unit_traits_dir[CP_PATH_MAX] = "", scientist_traits_dir[CP_PATH_MAX] = "";
    char history[CP_PATH_MAX] = "", countries[CP_PATH_MAX] = "";
    char country_traits[CP_PATH_MAX] = "", unit_traits[CP_PATH_MAX] = "";
    char scientist_traits[CP_PATH_MAX] = "", country_history[CP_PATH_MAX] = "";
    char image[CP_PATH_MAX] = "";
    char *character_text = NULL, *gfx_text = NULL, *history_text = NULL;
    CharacterCreateRequest request;
    CharacterCreateResult result = {0};
    CharacterCreateResult collision_result = {0};
    CharacterTraitCatalog catalog = {0};
    DWORD pid = GetCurrentProcessId();
    int large_w = 0, large_h = 0, small_w = 0, small_h = 0;
    unsigned char red = 0, green = 0, blue = 0;
    unsigned char corner_alpha = 255, paper_alpha = 0;
    int failure = 0;

    GetCurrentDirectoryA(sizeof(cwd), cwd);
    snprintf(root, sizeof(root), "%.4000s\\character-fixture-%lu",
             cwd, (unsigned long)pid);
    cp_path_join(game, sizeof(game), root, "game");
    cp_path_join(mod, sizeof(mod), root, "mod");
    CreateDirectoryA(root, NULL);
    CreateDirectoryA(game, NULL);
    CreateDirectoryA(mod, NULL);

    cp_path_join(common, sizeof(common), game, "common");
    CreateDirectoryA(common, NULL);
    cp_path_join(country_traits_dir, sizeof(country_traits_dir),
                 common, "country_leader");
    cp_path_join(unit_traits_dir, sizeof(unit_traits_dir), common, "unit_leader");
    cp_path_join(scientist_traits_dir, sizeof(scientist_traits_dir),
                 common, "scientist_traits");
    CreateDirectoryA(country_traits_dir, NULL);
    CreateDirectoryA(unit_traits_dir, NULL);
    CreateDirectoryA(scientist_traits_dir, NULL);
    cp_path_join(country_traits, sizeof(country_traits),
                 country_traits_dir, "traits.txt");
    cp_path_join(unit_traits, sizeof(unit_traits), unit_traits_dir, "traits.txt");
    cp_path_join(scientist_traits, sizeof(scientist_traits),
                 scientist_traits_dir, "traits.txt");
    write_fixture(country_traits,
                  "leader_traits = { fixture_country_trait = { random = no } }\r\n");
    write_fixture(unit_traits,
                  "leader_traits = { fixture_unit_trait = { type = land } }\r\n");
    write_fixture(scientist_traits,
                  "fixture_scientist_trait = { icon = GFX_fixture }\r\n");

    cp_path_join(history, sizeof(history), game, "history");
    CreateDirectoryA(history, NULL);
    cp_path_join(countries, sizeof(countries), history, "countries");
    CreateDirectoryA(countries, NULL);
    cp_path_join(country_history, sizeof(country_history),
                 countries, "AAA - Fixture.txt");
    write_fixture(country_history, "capital = 1\r\n");
    cp_path_join(image, sizeof(image), root, "portrait.bmp");
    if (!write_test_bmp(image)) {
        failure = 40;
        goto cleanup;
    }

    if (!character_trait_catalog_load(&catalog, game, mod)
        || catalog.count != 3) {
        fprintf(stderr, "Catalogue de traits incorrect: %zu (%s)\n",
                catalog.count, catalog.error);
        failure = 41;
        goto cleanup;
    }

    character_create_request_defaults(&request);
    snprintf(request.country_tag, sizeof(request.country_tag), "AAA");
    snprintf(request.token, sizeof(request.token), "AAA_jane_doe");
    snprintf(request.name, sizeof(request.name), "Jane \"Ace\" Doe");
    request.roles = CHARACTER_ROLE_COUNTRY_LEADER
        | CHARACTER_ROLE_ADVISOR | CHARACTER_ROLE_GENERAL
        | CHARACTER_ROLE_FIELD_MARSHAL | CHARACTER_ROLE_NAVY_LEADER
        | CHARACTER_ROLE_SCIENTIST;
    snprintf(request.country_traits, sizeof(request.country_traits),
             "fixture_country_trait");
    snprintf(request.advisor_traits, sizeof(request.advisor_traits),
             "fixture_country_trait");
    snprintf(request.land_traits, sizeof(request.land_traits),
             "fixture_unit_trait");
    snprintf(request.navy_traits, sizeof(request.navy_traits),
             "fixture_unit_trait");
    snprintf(request.scientist_traits, sizeof(request.scientist_traits),
             "fixture_scientist_trait");
    snprintf(request.large_portrait, sizeof(request.large_portrait), "%s", image);
    if (!character_create_execute(game, mod, &request, &result)) {
        fprintf(stderr, "Création de character impossible: %s\n", result.error);
        failure = 42;
        goto cleanup;
    }
    character_text = read_fixture(result.character_file);
    gfx_text = read_fixture(result.gfx_file);
    history_text = read_fixture(result.history_file);
    if (result.changed_files != 5
        || !character_text || !gfx_text || !history_text
        || !strstr(character_text, "AAA_jane_doe = {")
        || !strstr(character_text, "name = \"Jane \\\"Ace\\\" Doe\"")
        || !strstr(character_text, "country_leader = {")
        || !strstr(character_text, "advisor = {")
        || !strstr(character_text, "corps_commander = {")
        || !strstr(character_text, "field_marshal = {")
        || !strstr(character_text, "navy_leader = {")
        || !strstr(character_text, "scientist = {")
        || !strstr(gfx_text, "GFX_portrait_AAA_jane_doe_small")
        || !strstr(history_text, "capital = 1")
        || !strstr(history_text, "recruit_character = AAA_jane_doe")
        || !dds_dimensions(result.large_portrait_file, &large_w, &large_h)
        || !dds_dimensions(result.small_portrait_file, &small_w, &small_h)
        || large_w != 156 || large_h != 210 || small_w != 65 || small_h != 67
        || !dds_pixel(result.small_portrait_file, 0, 0,
                      &red, &green, &blue, &corner_alpha)
        || !dds_pixel(result.small_portrait_file, 45, 40,
                      &red, &green, &blue, &paper_alpha)
        || corner_alpha != 0 || paper_alpha == 0
        || !province_transfer_validate_syntax(character_text,
                                              result.error, sizeof(result.error))
        || !province_transfer_validate_syntax(gfx_text,
                                              result.error, sizeof(result.error))
        || !province_transfer_validate_syntax(history_text,
                                              result.error, sizeof(result.error))) {
        fprintf(stderr, "Sortie character invalide: %s\nCHAR:\n%s\nGFX:\n%s\n",
                result.error, character_text ? character_text : "(null)",
                gfx_text ? gfx_text : "(null)");
        failure = 43;
        goto cleanup;
    }
    if (character_create_execute(game, mod, &request, &collision_result)
        || !strstr(collision_result.error, "existe déjà")) {
        fprintf(stderr, "La collision de token n'a pas été refusée: %s\n",
                collision_result.error);
        failure = 44;
    }

cleanup:
    free(character_text);
    free(gfx_text);
    free(history_text);
    character_trait_catalog_free(&catalog);
    DeleteFileA(result.character_file);
    DeleteFileA(result.gfx_file);
    DeleteFileA(result.history_file);
    DeleteFileA(result.large_portrait_file);
    DeleteFileA(result.small_portrait_file);
    DeleteFileA(image);
    DeleteFileA(country_traits);
    DeleteFileA(unit_traits);
    DeleteFileA(scientist_traits);
    DeleteFileA(country_history);
    {
        char path[CP_PATH_MAX];
        cp_path_join(path, sizeof(path), mod, "common\\characters"); RemoveDirectoryA(path);
        cp_path_join(path, sizeof(path), mod, "common"); RemoveDirectoryA(path);
        cp_path_join(path, sizeof(path), mod, "interface"); RemoveDirectoryA(path);
        cp_path_join(path, sizeof(path), mod, "gfx\\leaders\\AAA"); RemoveDirectoryA(path);
        cp_path_join(path, sizeof(path), mod, "gfx\\leaders"); RemoveDirectoryA(path);
        cp_path_join(path, sizeof(path), mod, "gfx\\interface\\ideas"); RemoveDirectoryA(path);
        cp_path_join(path, sizeof(path), mod, "gfx\\interface"); RemoveDirectoryA(path);
        cp_path_join(path, sizeof(path), mod, "gfx"); RemoveDirectoryA(path);
        cp_path_join(path, sizeof(path), mod, "history\\countries"); RemoveDirectoryA(path);
        cp_path_join(path, sizeof(path), mod, "history"); RemoveDirectoryA(path);
    }
    RemoveDirectoryA(country_traits_dir);
    RemoveDirectoryA(unit_traits_dir);
    RemoveDirectoryA(scientist_traits_dir);
    RemoveDirectoryA(common);
    RemoveDirectoryA(countries);
    RemoveDirectoryA(history);
    RemoveDirectoryA(game);
    RemoveDirectoryA(mod);
    RemoveDirectoryA(root);
    return failure;
}

static int test_country_creator(void)
{
    char cwd[CP_PATH_MAX] = "", root[CP_PATH_MAX] = "";
    char game[CP_PATH_MAX] = "", mod[CP_PATH_MAX] = "";
    char path[CP_PATH_MAX] = "", common[CP_PATH_MAX] = "";
    char countries[CP_PATH_MAX] = "";
    CountryCreateRequest request;
    CountryCreateResult result = {0}, collision = {0};
    char *tags = NULL, *colors = NULL, *history = NULL, *loc = NULL;
    DWORD pid = GetCurrentProcessId();
    int failure = 0, i;

    GetCurrentDirectoryA(sizeof(cwd), cwd);
    snprintf(root, sizeof(root), "%.4000s\\country-fixture-%lu",
             cwd, (unsigned long)pid);
    cp_path_join(game, sizeof(game), root, "game");
    cp_path_join(mod, sizeof(mod), root, "mod");
    CreateDirectoryA(root, NULL);
    CreateDirectoryA(game, NULL);
    CreateDirectoryA(mod, NULL);
    cp_path_join(common, sizeof(common), game, "common");
    CreateDirectoryA(common, NULL);
    cp_path_join(countries, sizeof(countries), common, "countries");
    CreateDirectoryA(countries, NULL);
    cp_path_join(path, sizeof(path), countries, "_western_european.txt");
    write_fixture(path, "graphical_culture = western_european_gfx\n");

    country_create_request_defaults(&request);
    snprintf(request.tag, sizeof(request.tag), "ZQP");
    snprintf(request.name, sizeof(request.name), "Pays \"Test\"");
    snprintf(request.adjective, sizeof(request.adjective), "Testois");
    snprintf(request.definite_name, sizeof(request.definite_name),
             "le Pays \"Test\"");
    request.capital = 42;
    request.ruling_party = COUNTRY_IDEOLOGY_FASCISM;
    request.color[0] = 66; request.color[1] = 34; request.color[2] = 79;
    request.color_ui[0] = 82; request.color_ui[1] = 44; request.color_ui[2] = 99;
    request.create_placeholder_flags = true;
    cp_path_join(path, sizeof(path), countries, "colors.txt");
    write_fixture(path, "ZQP = { color = rgb { 1 2 3 } }\n");
    if (country_creator_validate(&request, game, mod,
                                 collision.error, sizeof(collision.error))
        || !strstr(collision.error, "vanilla")) {
        fprintf(stderr, "Le colors.txt vanilla n'a pas bloqué le tag: %s\n",
                collision.error);
        failure = 50;
        goto cleanup;
    }
    cp_path_join(path, sizeof(path), mod, "common");
    CreateDirectoryA(path, NULL);
    cp_path_join(path, sizeof(path), mod, "common\\countries");
    CreateDirectoryA(path, NULL);
    cp_path_join(path, sizeof(path), mod, "common\\countries\\colors.txt");
    write_fixture(path, "ABC = { color = rgb { 9 9 9 } }\n");
    if (!country_creator_execute(&request, game, mod, &result)) {
        fprintf(stderr, "Création de pays impossible: %s\n", result.error);
        failure = 50;
        goto cleanup;
    }
    tags = read_fixture(result.tag_file);
    colors = read_fixture(result.colors_file);
    history = read_fixture(result.history_file);
    loc = read_fixture(result.localisation_file);
    if (result.changed_files != 7 || !tags || !colors || !history || !loc
        || !strstr(tags, "ZQP = \"countries/_western_european.txt\"")
        || !strstr(colors, "color = rgb { 66 34 79 }")
        || !strstr(colors, "color_ui = rgb { 82 44 99 }")
        || !strstr(history, "capital = 42")
        || !strstr(history, "ruling_party = fascism")
        || !strstr(history, "fascism = 100")
        || (unsigned char)loc[0] != 0xEF
        || (unsigned char)loc[1] != 0xBB
        || (unsigned char)loc[2] != 0xBF
        || !strstr(loc, "ZQP_fascism: \"Pays \\\"Test\\\"\"")
        || !province_transfer_validate_syntax(colors,
                                              result.error, sizeof(result.error))
        || !province_transfer_validate_syntax(history,
                                              result.error, sizeof(result.error))) {
        fprintf(stderr, "Sortie pays invalide: %s\n", result.error);
        failure = 51;
        goto cleanup;
    }
    for (i = 0; i < 3; ++i) {
        FILE *flag = fopen(result.flag_files[i], "rb");
        unsigned char header[18];
        if (!flag || fread(header, 1, sizeof(header), flag) != sizeof(header)
            || header[2] != 2) {
            if (flag) fclose(flag);
            fprintf(stderr, "Drapeau pays invalide: %s\n", result.flag_files[i]);
            failure = 52;
            goto cleanup;
        }
        fclose(flag);
    }
    if (country_creator_execute(&request, game, mod, &collision)
        || !strstr(collision.error, "existe déjà")) {
        fprintf(stderr, "La collision de tag n'a pas été refusée: %s\n",
                collision.error);
        failure = 53;
    }

cleanup:
    free(tags); free(colors); free(history); free(loc);
    DeleteFileA(result.tag_file);
    DeleteFileA(result.colors_file);
    DeleteFileA(result.history_file);
    DeleteFileA(result.localisation_file);
    for (i = 0; i < 3; ++i) DeleteFileA(result.flag_files[i]);
    cp_path_join(path, sizeof(path), countries, "_western_european.txt");
    DeleteFileA(path);
    cp_path_join(path, sizeof(path), countries, "colors.txt");
    DeleteFileA(path);
    cp_path_join(path, sizeof(path), mod, "common\\country_tags"); RemoveDirectoryA(path);
    cp_path_join(path, sizeof(path), mod, "common\\countries"); RemoveDirectoryA(path);
    cp_path_join(path, sizeof(path), mod, "common"); RemoveDirectoryA(path);
    cp_path_join(path, sizeof(path), mod, "history\\countries"); RemoveDirectoryA(path);
    cp_path_join(path, sizeof(path), mod, "history"); RemoveDirectoryA(path);
    cp_path_join(path, sizeof(path), mod, "localisation\\english"); RemoveDirectoryA(path);
    cp_path_join(path, sizeof(path), mod, "localisation"); RemoveDirectoryA(path);
    cp_path_join(path, sizeof(path), mod, "gfx\\flags\\medium"); RemoveDirectoryA(path);
    cp_path_join(path, sizeof(path), mod, "gfx\\flags\\small"); RemoveDirectoryA(path);
    cp_path_join(path, sizeof(path), mod, "gfx\\flags"); RemoveDirectoryA(path);
    cp_path_join(path, sizeof(path), mod, "gfx"); RemoveDirectoryA(path);
    RemoveDirectoryA(countries);
    RemoveDirectoryA(common);
    RemoveDirectoryA(game);
    RemoveDirectoryA(mod);
    RemoveDirectoryA(root);
    return failure;
}

int main(int argc, char **argv)
{
    char path[CP_PATH_MAX];
    int edit_result = test_state_edit();
    if (edit_result) return edit_result;
    edit_result = test_province_transfer();
    if (edit_result) return edit_result;
    edit_result = test_character_creator();
    if (edit_result) return edit_result;
    edit_result = test_country_creator();
    if (edit_result) return edit_result;
    if (!cp_path_join(path, sizeof(path), "C:\\Games\\HOI4", "map\\provinces.bmp")) return 1;
    if (strcmp(path, "C:\\Games\\HOI4\\map\\provinces.bmp") != 0) {
        fprintf(stderr, "Chemin inattendu: %s\n", path);
        return 1;
    }
    if (argc > 1) {
        Hoi4Map map;
        CharacterTraitCatalog live_traits = {0};
        const Hoi4State *state;
        size_t i;
        size_t vp_count = 0;
        size_t state_pixel = (size_t)-1;
        uint32_t before;
        if (!character_trait_catalog_load(&live_traits, argv[1],
                                          argc > 2 ? argv[2] : "")
            || live_traits.count < 10) {
            fprintf(stderr, "Catalogue réel de traits invalide: %zu (%s)\n",
                    live_traits.count, live_traits.error);
            character_trait_catalog_free(&live_traits);
            return 18;
        }
        printf("traits=%zu\n", live_traits.count);
        character_trait_catalog_free(&live_traits);
        hoi4_map_init(&map);
        if (!hoi4_map_load(&map, argv[1], argc > 2 ? argv[2] : "")) {
            fprintf(stderr, "Chargement impossible: %s\n", map.error);
            hoi4_map_free(&map);
            return 2;
        }
        state = hoi4_map_state(&map, 1);
        printf("map=%dx%d provinces=%zu states=%zu countries=%zu\n",
               map.width, map.height, map.loaded_province_count,
               map.loaded_state_count, map.country_count);
        if (!state || strcmp(state->owner, "FRA") != 0 || state->province_count == 0) {
            fprintf(stderr, "L'état 1 est absent ou incorrect\n");
            hoi4_map_free(&map);
            return 3;
        }
        for (i = 0; i < (size_t)map.width * map.height; ++i) {
            if (map.state_at[i] == 1) {
                state_pixel = i;
                break;
            }
        }
        if (state_pixel == (size_t)-1) {
            fprintf(stderr, "Aucun pixel ne correspond à l'état 1\n");
            hoi4_map_free(&map);
            return 4;
        }
        before = map.pixels[state_pixel];
        hoi4_map_set_hover(&map, HOI4_VIEW_STATES, 0, 1);
        if (map.pixels[state_pixel] == before) {
            fprintf(stderr, "La surbrillance n'a pas modifié l'état 1\n");
            hoi4_map_free(&map);
            return 5;
        }
        hoi4_map_set_hover(&map, HOI4_VIEW_STATES, 1, 0);
        if (map.pixels[state_pixel] != before) {
            fprintf(stderr, "La surbrillance n'a pas restauré l'état 1\n");
            hoi4_map_free(&map);
            return 6;
        }
        if (map.loaded_strategic_region_count == 0
            || !hoi4_map_strategic_region(&map, 1)
            || !hoi4_map_province(&map, state->provinces[0])) {
            fprintf(stderr, "Les provinces ou régions stratégiques sont absentes\n");
            hoi4_map_free(&map);
            return 7;
        }
        for (i = 1; i < HOI4_MAX_STATES; ++i) {
            const Hoi4State *vp_state = hoi4_map_state(&map, (int)i);
            size_t q;
            if (!vp_state) continue;
            for (q = 0; q < vp_state->victory_point_count; ++q) {
                const Hoi4VictoryPoint *vp = &vp_state->victory_points[q];
                if (vp->value <= 0 || vp->map_x < 0 || vp->map_y < 0) {
                    fprintf(stderr, "Victory point invalide dans l'état %zu.\n", i);
                    hoi4_map_free(&map);
                    return 19;
                }
                vp_count++;
            }
        }
        if (vp_count < 100) {
            fprintf(stderr, "Trop peu de victory points chargés: %zu\n", vp_count);
            hoi4_map_free(&map);
            return 19;
        }
        printf("victory_points=%zu\n", vp_count);
        hoi4_map_render_mode(&map, HOI4_VIEW_PROVINCES);
        hoi4_map_render_mode(&map, HOI4_VIEW_STRATEGIC_REGIONS);
        hoi4_map_render_mode(&map, HOI4_VIEW_STATES);
        printf("state1=%s owner=%s province_count=%zu pixel=%08X\n",
               state->name, state->owner, state->province_count,
               (unsigned)map.base_pixels[(size_t)map.height / 2 * map.width]);
        if (argc > 2) {
            uint8_t *selected = calloc(HOI4_MAX_PROVINCES, 1);
            ProvinceTransferResult transfer;
            Hoi4Map verify;
            int moved = state->provinces[0];
            const Hoi4State *verified_source, *verified_target;
            if (!selected) {
                hoi4_map_free(&map);
                return 8;
            }
            selected[moved] = 1;
            if (!province_transfer_execute(&map, argv[2], selected, 2, &transfer)) {
                fprintf(stderr, "Transfert réel impossible: %s\n", transfer.error);
                free(selected); hoi4_map_free(&map);
                return 9;
            }
            free(selected);
            hoi4_map_init(&verify);
            if (!hoi4_map_load(&verify, argv[1], argv[2])) {
                fprintf(stderr, "Rechargement après transfert impossible: %s\n", verify.error);
                hoi4_map_free(&verify); hoi4_map_free(&map);
                return 16;
            }
            verified_source = hoi4_map_state(&verify, 1);
            verified_target = hoi4_map_state(&verify, 2);
            if (!verified_source || !verified_target) {
                fprintf(stderr, "Les états de vérification sont absents.\n");
                hoi4_map_free(&verify); hoi4_map_free(&map);
                return 17;
            }
            {
                bool in_source = false, in_target = false;
                size_t q;
                for (q = 0; q < verified_source->province_count; ++q)
                    if (verified_source->provinces[q] == moved) in_source = true;
                for (q = 0; q < verified_target->province_count; ++q)
                    if (verified_target->provinces[q] == moved) in_target = true;
                if (in_source || !in_target) {
                    fprintf(stderr, "Le transfert réel n'est pas visible après rechargement.\n");
                    hoi4_map_free(&verify); hoi4_map_free(&map);
                    return 17;
                }
            }
            printf("transfer province=%d target=2 regions_changed=%zu reload=ok\n",
                   moved, transfer.changed_strategic_regions);
            hoi4_map_free(&verify);
        }
        hoi4_map_free(&map);
    }
    return 0;
}
