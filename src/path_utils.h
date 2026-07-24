#ifndef CRISPY_PATH_UTILS_H
#define CRISPY_PATH_UTILS_H

#include <stdbool.h>
#include <stddef.h>

#define CP_PATH_MAX 4096

bool cp_path_exists(const char *path);
bool cp_path_is_dir(const char *path);
bool cp_path_join(char *out, size_t size, const char *left, const char *right);
bool cp_find_hoi4(char *out, size_t size);
bool cp_choose_folder(const char *title, const char *initial, char *out, size_t size);
bool cp_open_file(const char *path);
bool cp_load_settings(char *game_root, size_t game_size, char *mod_root, size_t mod_size);
bool cp_save_settings(const char *game_root, const char *mod_root);

typedef bool (*CpFileVisitor)(const char *path, const char *name, void *user);
bool cp_visit_txt_files(const char *directory, CpFileVisitor visitor, void *user);

#endif
