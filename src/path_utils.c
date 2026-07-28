#include "path_utils.h"

#include <windows.h>
#include <shlobj.h>
#include <shellapi.h>
#include <commdlg.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool cp_path_exists(const char *path)
{
    DWORD attrs = GetFileAttributesA(path);
    return attrs != INVALID_FILE_ATTRIBUTES;
}

bool cp_path_is_dir(const char *path)
{
    DWORD attrs = GetFileAttributesA(path);
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

bool cp_path_join(char *out, size_t size, const char *left, const char *right)
{
    size_t left_len;
    int written;
    if (!out || !left || !right || size == 0) {
        return false;
    }
    left_len = strlen(left);
    written = snprintf(out, size, "%s%s%s", left,
                       left_len > 0 && left[left_len - 1] != '\\' && left[left_len - 1] != '/' ? "\\" : "",
                       right);
    return written >= 0 && (size_t)written < size;
}

static bool valid_hoi4_root(const char *root)
{
    char path[CP_PATH_MAX];
    return cp_path_join(path, sizeof(path), root, "map\\provinces.bmp") && cp_path_exists(path)
        && cp_path_join(path, sizeof(path), root, "map\\definition.csv") && cp_path_exists(path)
        && cp_path_join(path, sizeof(path), root, "history\\states") && cp_path_is_dir(path);
}

static bool extract_vdf_paths(const char *vdf, char *out, size_t size)
{
    FILE *file = fopen(vdf, "rb");
    long length;
    char *text;
    char *cursor;
    if (!file) {
        return false;
    }
    fseek(file, 0, SEEK_END);
    length = ftell(file);
    rewind(file);
    text = malloc((size_t)length + 1);
    if (!text || fread(text, 1, (size_t)length, file) != (size_t)length) {
        free(text);
        fclose(file);
        return false;
    }
    fclose(file);
    text[length] = '\0';
    cursor = text;
    while ((cursor = strstr(cursor, "\"path\"")) != NULL) {
        char *start = strchr(cursor + 6, '"');
        char *end;
        char library[CP_PATH_MAX];
        char game[CP_PATH_MAX];
        size_t n = 0;
        if (!start) break;
        start++;
        end = strchr(start, '"');
        if (!end) break;
        while (start < end && n + 1 < sizeof(library)) {
            if (start + 1 < end && start[0] == '\\' && start[1] == '\\') start++;
            library[n++] = *start++;
        }
        library[n] = '\0';
        if (cp_path_join(game, sizeof(game), library, "steamapps\\common\\Hearts of Iron IV")
            && valid_hoi4_root(game)) {
            snprintf(out, size, "%s", game);
            free(text);
            return true;
        }
        cursor = end + 1;
    }
    free(text);
    return false;
}

bool cp_find_hoi4(char *out, size_t size)
{
    static const char *fallbacks[] = {
        "C:\\Program Files (x86)\\Steam\\steamapps\\common\\Hearts of Iron IV",
        "C:\\Program Files\\Steam\\steamapps\\common\\Hearts of Iron IV",
        "D:\\SteamLibrary\\steamapps\\common\\Hearts of Iron IV",
        "E:\\SteamLibrary\\steamapps\\common\\Hearts of Iron IV",
        "F:\\SteamLibrary\\steamapps\\common\\Hearts of Iron IV"
    };
    HKEY key;
    char steam[CP_PATH_MAX] = "";
    DWORD bytes = sizeof(steam);
    size_t i;

    if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\Valve\\Steam", 0, KEY_READ, &key) == ERROR_SUCCESS) {
        RegQueryValueExA(key, "SteamPath", NULL, NULL, (BYTE *)steam, &bytes);
        RegCloseKey(key);
    }
    if (steam[0]) {
        char game[CP_PATH_MAX];
        char vdf[CP_PATH_MAX];
        if (cp_path_join(game, sizeof(game), steam, "steamapps\\common\\Hearts of Iron IV")
            && valid_hoi4_root(game)) {
            snprintf(out, size, "%s", game);
            return true;
        }
        if (cp_path_join(vdf, sizeof(vdf), steam, "steamapps\\libraryfolders.vdf")
            && extract_vdf_paths(vdf, out, size)) {
            return true;
        }
    }
    for (i = 0; i < sizeof(fallbacks) / sizeof(fallbacks[0]); ++i) {
        if (valid_hoi4_root(fallbacks[i])) {
            snprintf(out, size, "%s", fallbacks[i]);
            return true;
        }
    }
    return false;
}

bool cp_choose_folder(const char *title, const char *initial, char *out, size_t size)
{
    BROWSEINFOA info = {0};
    PIDLIST_ABSOLUTE item;
    char selected[MAX_PATH];
    (void)initial;
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    info.lpszTitle = title;
    info.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE | BIF_USENEWUI;
    item = SHBrowseForFolderA(&info);
    if (!item) {
        CoUninitialize();
        return false;
    }
    if (!SHGetPathFromIDListA(item, selected)) {
        CoTaskMemFree(item);
        CoUninitialize();
        return false;
    }
    CoTaskMemFree(item);
    CoUninitialize();
    snprintf(out, size, "%s", selected);
    return true;
}

bool cp_choose_image_file(const char *title, char *out, size_t size)
{
    OPENFILENAMEA dialog = {0};
    char selected[CP_PATH_MAX] = "";
    dialog.lStructSize = sizeof(dialog);
    dialog.lpstrTitle = title;
    dialog.lpstrFile = selected;
    dialog.nMaxFile = sizeof(selected);
    dialog.lpstrFilter =
        "Images (*.png;*.jpg;*.jpeg;*.bmp;*.tif;*.tiff)\0"
        "*.png;*.jpg;*.jpeg;*.bmp;*.tif;*.tiff\0"
        "Tous les fichiers (*.*)\0*.*\0\0";
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameA(&dialog)) return false;
    snprintf(out, size, "%s", selected);
    return true;
}

bool cp_open_file(const char *path)
{
    HINSTANCE result;
    if (!path || !path[0] || !cp_path_exists(path)) return false;
    result = ShellExecuteA(NULL, "open", path, NULL, NULL, SW_SHOWNORMAL);
    return (INT_PTR)result > 32;
}

static bool settings_path(char *out, size_t size, bool create_directory)
{
    char app_data[CP_PATH_MAX];
    char directory[CP_PATH_MAX];
    if (SHGetFolderPathA(NULL, CSIDL_APPDATA | (create_directory ? CSIDL_FLAG_CREATE : 0),
                         NULL, SHGFP_TYPE_CURRENT, app_data) != S_OK) {
        return false;
    }
    if (!cp_path_join(directory, sizeof(directory), app_data, "CrispyPandas")) return false;
    if (create_directory && !CreateDirectoryA(directory, NULL)
        && GetLastError() != ERROR_ALREADY_EXISTS) {
        return false;
    }
    return cp_path_join(out, size, directory, "settings.ini");
}

bool cp_load_settings(char *game_root, size_t game_size, char *mod_root, size_t mod_size)
{
    char path[CP_PATH_MAX];
    char line[CP_PATH_MAX + 16];
    FILE *file;
    bool found = false;
    if (!settings_path(path, sizeof(path), false) || !(file = fopen(path, "rb"))) return false;
    while (fgets(line, sizeof(line), file)) {
        char *value;
        size_t length = strlen(line);
        while (length > 0 && (line[length - 1] == '\r' || line[length - 1] == '\n'))
            line[--length] = '\0';
        if (strncmp(line, "game=", 5) == 0) {
            value = line + 5;
            snprintf(game_root, game_size, "%s", value);
            found = true;
        } else if (strncmp(line, "mod=", 4) == 0) {
            value = line + 4;
            snprintf(mod_root, mod_size, "%s", value);
            found = true;
        }
    }
    fclose(file);
    return found;
}

bool cp_save_settings(const char *game_root, const char *mod_root)
{
    char path[CP_PATH_MAX];
    FILE *file;
    if (!settings_path(path, sizeof(path), true) || !(file = fopen(path, "wb"))) return false;
    fprintf(file, "game=%s\nmod=%s\n", game_root ? game_root : "", mod_root ? mod_root : "");
    return fclose(file) == 0;
}

bool cp_visit_txt_files(const char *directory, CpFileVisitor visitor, void *user)
{
    char pattern[CP_PATH_MAX];
    WIN32_FIND_DATAA data;
    HANDLE find;
    bool ok = true;
    if (!cp_path_join(pattern, sizeof(pattern), directory, "*.txt")) return false;
    find = FindFirstFileA(pattern, &data);
    if (find == INVALID_HANDLE_VALUE) return false;
    do {
        char path[CP_PATH_MAX];
        if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0
            && cp_path_join(path, sizeof(path), directory, data.cFileName)
            && !visitor(path, data.cFileName, user)) {
            ok = false;
            break;
        }
    } while (FindNextFileA(find, &data));
    FindClose(find);
    return ok;
}
