#include "hoi4_map.h"
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
        if (!province_state_create_execute(&map, mod, selected, &create_result)) {
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
            || !strstr(localization_result, "STATE_3:0 \"New State 3\"")
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

int main(int argc, char **argv)
{
    char path[CP_PATH_MAX];
    int edit_result = test_state_edit();
    if (edit_result) return edit_result;
    edit_result = test_province_transfer();
    if (edit_result) return edit_result;
    if (!cp_path_join(path, sizeof(path), "C:\\Games\\HOI4", "map\\provinces.bmp")) return 1;
    if (strcmp(path, "C:\\Games\\HOI4\\map\\provinces.bmp") != 0) {
        fprintf(stderr, "Chemin inattendu: %s\n", path);
        return 1;
    }
    if (argc > 1) {
        Hoi4Map map;
        const Hoi4State *state;
        size_t i;
        size_t state_pixel = (size_t)-1;
        uint32_t before;
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
