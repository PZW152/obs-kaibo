#pragma once
#define LOG_ERROR 100
#define LOG_WARNING 200
#define LOG_INFO 300
#define LOG_DEBUG 400
#define PLUGIN_VERSION "preview"
#ifdef __cplusplus
extern "C" {
#endif
void obs_log(int level, const char *format, ...);
#ifdef __cplusplus
}
#endif
