#include <psp2/kernel/modulemgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/io/fcntl.h>
#include <psp2/motion.h>
#include <taihen.h>

#define CFG_PATH "ux0:data/gyroicons.cfg"
#define LOG_PATH "ux0:data/gyroicons_log.txt"

static volatile float g_level = 0.0f;
static volatile float g_vel_x = 0.0f;
static volatile float g_vel_y = 0.0f;
static volatile int g_run = 1;

static SceUID g_thread = -1;
static SceUID g_hook = -1;
static tai_hook_ref_t g_ref;

static float g_threshold = 0.35f;
static float g_attack = 0.35f;
static float g_decay = 0.06f;
static float g_minrate = 0.0f;
static int g_log = 1;
static unsigned int g_offset = 0;
static int g_thumb = 1;

static int str_eq_prefix(const char *s, const char *p) {
    while (*p) {
        if (*s++ != *p++) return 0;
    }
    return 1;
}

static float parse_float(const char *s) {
    float sign = 1.0f, v = 0.0f, scale = 0.1f;
    if (*s == '-') { sign = -1.0f; s++; }
    while (*s >= '0' && *s <= '9') { v = v * 10.0f + (float)(*s - '0'); s++; }
    if (*s == '.') {
        s++;
        while (*s >= '0' && *s <= '9') {
            v += (float)(*s - '0') * scale;
            scale *= 0.1f;
            s++;
        }
    }
    return sign * v;
}

static unsigned int parse_uint(const char *s) {
    unsigned int v = 0;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
        for (;; s++) {
            char c = *s;
            unsigned int d;
            if (c >= '0' && c <= '9') d = (unsigned int)(c - '0');
            else if (c >= 'a' && c <= 'f') d = (unsigned int)(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') d = (unsigned int)(c - 'A' + 10);
            else break;
            v = v * 16u + d;
        }
    } else {
        while (*s >= '0' && *s <= '9') {
            v = v * 10u + (unsigned int)(*s - '0');
            s++;
        }
    }
    return v;
}

static void apply_line(const char *l) {
    if (str_eq_prefix(l, "threshold=")) g_threshold = parse_float(l + 10);
    else if (str_eq_prefix(l, "attack=")) g_attack = parse_float(l + 7);
    else if (str_eq_prefix(l, "decay=")) g_decay = parse_float(l + 6);
    else if (str_eq_prefix(l, "minrate=")) g_minrate = parse_float(l + 8);
    else if (str_eq_prefix(l, "log=")) g_log = (int)parse_uint(l + 4);
    else if (str_eq_prefix(l, "offset=")) g_offset = parse_uint(l + 7);
    else if (str_eq_prefix(l, "thumb=")) g_thumb = (int)parse_uint(l + 6);
}

static void load_cfg(void) {
    char buf[512];
    SceUID fd = sceIoOpen(CFG_PATH, SCE_O_RDONLY, 0);
    if (fd < 0) return;
    int n = sceIoRead(fd, buf, sizeof(buf) - 1);
    sceIoClose(fd);
    if (n <= 0) return;
    buf[n] = '\0';

    char *p = buf;
    while (*p) {
        char *line = p;
        while (*p && *p != '\n' && *p != '\r') p++;
        if (*p) { *p = '\0'; p++; }
        while (*p == '\n' || *p == '\r') p++;
        if (*line) apply_line(line);
    }

    if (g_minrate < 0.0f) g_minrate = 0.0f;
    if (g_minrate > 1.0f) g_minrate = 1.0f;
}

static int target_ui_hook(unsigned int a0, unsigned int a1, unsigned int a2, unsigned int a3) {
    (void)g_level;
    (void)g_vel_x;
    (void)g_vel_y;
    return TAI_CONTINUE(int, g_ref, a0, a1, a2, a3);
}

static int sensor_thread(SceSize args, void *argp) {
    (void)args;
    (void)argp;

    SceMotionState ms;
    float level = 0.0f;
    SceUID log_fd = -1;
    int tick = 0;

    sceMotionStartSampling();

    if (g_log) {
        log_fd = sceIoOpen(LOG_PATH, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
    }

    while (g_run) {
        if (sceMotionGetState(&ms) == 0) {
            float x = ms.angularVelocity.x;
            float y = ms.angularVelocity.y;
            float z = ms.angularVelocity.z;
            float mag2 = x * x + y * y + z * z;
            float thr2 = g_threshold * g_threshold;

            float target = (mag2 > thr2) ? 1.0f : 0.0f;
            float k = (target > level) ? g_attack : g_decay;
            level += (target - level) * k;
            if (level < 0.001f) level = 0.0f;

            g_level = level;
            g_vel_x = x;
            g_vel_y = y;

            if (log_fd >= 0 && (++tick % 10) == 0) {
                char line[128];
                int len = 0;
                int m_int = (int)(mag2 * 1000.0f);
                int l_int = (int)(level * 1000.0f);
                line[len++] = 'm'; line[len++] = '=';
                if (m_int < 0) { line[len++] = '-'; m_int = -m_int; }
                char tmp[16];
                int ti = 0;
                do { tmp[ti++] = (char)('0' + (m_int % 10)); m_int /= 10; } while (m_int > 0);
                while (ti > 0) line[len++] = tmp[--ti];
                line[len++] = ' '; line[len++] = 'l'; line[len++] = '=';
                ti = 0;
                do { tmp[ti++] = (char)('0' + (l_int % 10)); l_int /= 10; } while (l_int > 0);
                while (ti > 0) line[len++] = tmp[--ti];
                line[len++] = '\n';
                sceIoWrite(log_fd, line, (SceSize)len);
            }
        }
        sceKernelDelayThread(16000);
    }

    if (log_fd >= 0) sceIoClose(log_fd);
    sceMotionStopSampling();
    return 0;
}

int _start(SceSize args, const void *argp) __attribute__((weak, alias("module_start")));

int module_start(SceSize args, const void *argp) {
    (void)args;
    (void)argp;

    load_cfg();

    g_thread = sceKernelCreateThread("gyroicons_sensor", sensor_thread, 0x10000100, 0x4000, 0, 0, NULL);
    if (g_thread >= 0) sceKernelStartThread(g_thread, 0, NULL);

    if (g_offset != 0) {
        tai_module_info_t info;
        info.size = sizeof(info);
        if (taiGetModuleInfo("SceShell", &info) >= 0) {
            g_hook = taiHookFunctionOffset(&g_ref, info.modid, 0, g_offset, g_thumb ? 1 : 0, target_ui_hook);
        }
    }

    return SCE_KERNEL_START_SUCCESS;
}

int module_stop(SceSize args, const void *argp) {
    (void)args;
    (void)argp;

    g_run = 0;
    if (g_thread >= 0) {
        sceKernelWaitThreadEnd(g_thread, NULL, NULL);
        sceKernelDeleteThread(g_thread);
    }
    if (g_hook >= 0) {
        taiHookRelease(g_hook, g_ref);
    }
    return SCE_KERNEL_STOP_SUCCESS;
}
