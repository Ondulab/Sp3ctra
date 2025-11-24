/* logger.c */

#include "logger.h"
#include <time.h>
#include <pthread.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

log_level_t g_log_level = LOG_LEVEL_DEBUG;
startup_verbose_t g_startup_verbose = STARTUP_VERBOSE_NORMAL;
static pthread_mutex_t g_log_mutex = PTHREAD_MUTEX_INITIALIZER;
static int g_colors_enabled = -1; // -1 = not initialized, 0 = disabled, 1 = enabled
static int g_startup_verbose_initialized = 0;

// ANSI color codes
#define ANSI_RESET          "\033[0m"
#define ANSI_BOLD           "\033[1m"
#define ANSI_DIM            "\033[2m"

// Level colors
#define ANSI_RED            "\033[91m"  // Bright red for errors
#define ANSI_YELLOW         "\033[93m"  // Bright yellow for warnings
#define ANSI_CYAN           "\033[96m"  // Bright cyan for info
#define ANSI_GRAY           "\033[90m"  // Gray for debug

// Module colors (using different hues for visual differentiation)
#define ANSI_MODULE_BLUE    "\033[94m"  // Bright blue
#define ANSI_MODULE_MAGENTA "\033[95m"  // Bright magenta
#define ANSI_MODULE_GREEN   "\033[92m"  // Bright green
#define ANSI_MODULE_YELLOW  "\033[93m"  // Bright yellow
#define ANSI_MODULE_CYAN    "\033[96m"  // Bright cyan
#define ANSI_MODULE_WHITE   "\033[97m"  // Bright white

// Time color
#define ANSI_TIME_DIM       "\033[2;37m" // Dim white for timestamp

static void init_startup_verbose(void) {
    if (g_startup_verbose_initialized) return;
    
    const char* env = getenv("SP3CTRA_STARTUP_VERBOSE");
    if (env != NULL) {
        int val = atoi(env);
        if (val == 0) {
            g_startup_verbose = STARTUP_VERBOSE_MINIMAL;
        } else if (val == 2) {
            g_startup_verbose = STARTUP_VERBOSE_FULL;
        } else {
            g_startup_verbose = STARTUP_VERBOSE_NORMAL;
        }
    } else {
        g_startup_verbose = STARTUP_VERBOSE_NORMAL;
    }
    
    g_startup_verbose_initialized = 1;
}

static void init_colors(void) {
    if (g_colors_enabled != -1) return;
    
    // Check if NO_COLOR or SP3CTRA_NO_COLOR environment variable is set
    if (getenv("NO_COLOR") != NULL || getenv("SP3CTRA_NO_COLOR") != NULL) {
        g_colors_enabled = 0;
        return;
    }
    
    // Check if stderr is a terminal
    g_colors_enabled = isatty(STDERR_FILENO) ? 1 : 0;
}

static const char* get_color_for_level(log_level_t level) {
    if (!g_colors_enabled) return "";
    
    switch (level) {
        case LOG_LEVEL_ERROR:   return ANSI_RED;
        case LOG_LEVEL_WARNING: return ANSI_YELLOW;
        case LOG_LEVEL_INFO:    return ANSI_CYAN;
        case LOG_LEVEL_DEBUG:   return ANSI_GRAY;
        default:                return "";
    }
}

static const char* get_color_for_module(const char* module) {
    if (!g_colors_enabled) return "";
    
    // Simple hash-based color selection for consistent module colors
    unsigned int hash = 0;
    for (const char* p = module; *p; p++) {
        hash = hash * 31 + (unsigned char)*p;
    }
    
    // Map to one of 6 distinct colors
    switch (hash % 6) {
        case 0: return ANSI_MODULE_BLUE;
        case 1: return ANSI_MODULE_MAGENTA;
        case 2: return ANSI_MODULE_GREEN;
        case 3: return ANSI_MODULE_YELLOW;
        case 4: return ANSI_MODULE_CYAN;
        case 5: return ANSI_MODULE_WHITE;
        default: return ANSI_MODULE_WHITE;
    }
}

static const char* get_reset_code(void) {
    return g_colors_enabled ? ANSI_RESET : "";
}

static const char* get_time_color(void) {
    return g_colors_enabled ? ANSI_TIME_DIM : "";
}

void logger_init(log_level_t level) {
    g_log_level = level;
    init_colors();
    init_startup_verbose();
}

int is_startup_verbose(void) {
    init_startup_verbose();
    return (g_startup_verbose == STARTUP_VERBOSE_FULL);
}

static const char* level_string(log_level_t level) {
    switch (level) {
        case LOG_LEVEL_ERROR:   return "ERROR";
        case LOG_LEVEL_WARNING: return "WARNING";
        case LOG_LEVEL_INFO:    return "INFO";
        case LOG_LEVEL_DEBUG:   return "DEBUG";
        default:                return "UNKNOWN";
    }
}

static void log_message(log_level_t level, const char* module, const char* fmt, va_list args) {
    if (level > g_log_level) return;
    
    init_colors();
    
    pthread_mutex_lock(&g_log_mutex);
    
    time_t now = time(NULL);
    struct tm* t = localtime(&now);
    
    const char* level_color = get_color_for_level(level);
    const char* module_color = get_color_for_module(module);
    const char* time_color = get_time_color();
    const char* reset = get_reset_code();
    
    // Print timestamp in dim color
    fprintf(stderr, "%s[%02d:%02d:%02d]%s ",
            time_color, t->tm_hour, t->tm_min, t->tm_sec, reset);
    
    // Print level with its color
    fprintf(stderr, "%s[%s]%s ",
            level_color, level_string(level), reset);
    
    // Print module with its unique color
    fprintf(stderr, "%s[%s]%s ",
            module_color, module, reset);
    
    // Print message content
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    fflush(stderr);
    
    pthread_mutex_unlock(&g_log_mutex);
}

void log_error(const char* module, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    log_message(LOG_LEVEL_ERROR, module, fmt, args);
    va_end(args);
}

void log_warning(const char* module, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    log_message(LOG_LEVEL_WARNING, module, fmt, args);
    va_end(args);
}

void log_info(const char* module, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    log_message(LOG_LEVEL_INFO, module, fmt, args);
    va_end(args);
}

void log_debug(const char* module, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    log_message(LOG_LEVEL_DEBUG, module, fmt, args);
    va_end(args);
}

void config_log_error(int line, const char* fmt, ...) {
    if (LOG_LEVEL_ERROR > g_log_level) return;
    
    init_colors();
    
    pthread_mutex_lock(&g_log_mutex);
    
    const char* level_color = get_color_for_level(LOG_LEVEL_ERROR);
    const char* module_color = get_color_for_module("CONFIG");
    const char* reset = get_reset_code();
    
    fprintf(stderr, "%s[CONFIG ERROR]%s %sLine %d:%s ", 
            level_color, reset, module_color, line, reset);
    
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, "\n");
    fflush(stderr);
    pthread_mutex_unlock(&g_log_mutex);
}

void config_log_warning(int line, const char* fmt, ...) {
    if (LOG_LEVEL_WARNING > g_log_level) return;
    
    init_colors();
    
    pthread_mutex_lock(&g_log_mutex);
    
    const char* level_color = get_color_for_level(LOG_LEVEL_WARNING);
    const char* module_color = get_color_for_module("CONFIG");
    const char* reset = get_reset_code();
    
    fprintf(stderr, "%s[CONFIG WARNING]%s %sLine %d:%s ", 
            level_color, reset, module_color, line, reset);
    
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, "\n");
    fflush(stderr);
    pthread_mutex_unlock(&g_log_mutex);
}

void config_log_info(int line, const char* fmt, ...) {
    if (LOG_LEVEL_INFO > g_log_level) return;
    
    init_colors();
    
    pthread_mutex_lock(&g_log_mutex);
    
    const char* level_color = get_color_for_level(LOG_LEVEL_INFO);
    const char* module_color = get_color_for_module("CONFIG");
    const char* reset = get_reset_code();
    
    fprintf(stderr, "%s[CONFIG INFO]%s %sLine %d:%s ", 
            level_color, reset, module_color, line, reset);
    
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, "\n");
    fflush(stderr);
    pthread_mutex_unlock(&g_log_mutex);
}
