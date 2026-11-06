#pragma once
#ifdef __cplusplus
extern "C" {
#endif
const char *obs_module_text(const char *key);
const char *obs_get_locale(void);
char *obs_module_file(const char *file);
char *obs_module_config_path(const char *file);
#ifdef __cplusplus
}
#endif
