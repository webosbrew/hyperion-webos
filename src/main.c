#include <getopt.h>
#include <glib.h>

#include "log.h"
#include "service.h"
#include "settings.h"

GMainLoop* loop;

settings_t settings;
service_t service;
bool using_cli = false;

void int_handler(int dummy)
{
    INFO("SIGINT detected, stopping...");
    g_main_loop_quit(loop);
}

static struct option long_options[] = {
    { "width", required_argument, 0, 'x' },
    { "height", required_argument, 0, 'y' },
    { "address", required_argument, 0, 'a' },
    { "port", required_argument, 0, 'p' },
    { "fps", required_argument, 0, 'f' },
    { "no-video", no_argument, 0, 'V' },
    { "no-gui", no_argument, 0, 'G' },
    { "no-service", no_argument, 0, 'S' },
    { "no-vsync", no_argument, 0, 'n' },
    { "backend", required_argument, 0, 'b' },
    { "ui-backend", required_argument, 0, 'u' },
    { "help", no_argument, 0, 'h' },
    { "verbose", no_argument, 0, 'v' },
    { "config", required_argument, 0, 'c' },
    { "save-conf", required_argument, 0, 's' },
    { 0, 0, 0, 0 },
};

static void print_usage()
{
    printf("Usage: hyperion-webos -a ADDRESS -S [OPTION]...\n");
    printf("\n");
    printf("Grab screen content continously and send to Hyperion via flatbuffers server.\n");
    printf("Application has to be named to hyperion-webos to avoid bugs!\n");
    printf("\n");
    printf("  -S, --no-service      Run this from CLI and not as webOS-Service\n");
    printf("  -x, --width=WIDTH     Width of video frame (default 192)\n");
    printf("  -y, --height=HEIGHT   Height of video frame (default 108)\n");
    printf("  -a, --address=ADDR    IP address of Hyperion server\n");
    printf("  -p, --port=PORT       Port of Hyperion flatbuffers server (default 19400)\n");
    printf("  -f, --fps=FPS         Framerate for sending video frames (default 0 = unlimited)\n");
    printf("  -b, --backend=BE      Use specific video capture backend (default auto)\n");
    printf("  -u, --ui-backend=BE   Use specific ui capture backend (default auto)\n");
    printf("  -V, --no-video        Video will not be captured\n");
    printf("  -G, --no-gui          GUI/UI will not be captured\n");
    printf("  -n, --no-vsync        Disable vsync (may increase framerate at the cost of tearing/artifacts)\n");
    printf("  -c, --config=PATH     Absolute path for configfile to load settings. Giving additional runtime arguments will overwrite loaded ones.\n");
    printf("  -s, --save-conf=PATH  Saving configfile to given path.\n");
}

static int parse_options(int argc, char* argv[])
{
    int opt, longindex;
    int ret;

    while ((opt = getopt_long(argc, argv, "x:y:a:p:f:b:u:c:s:vnhSVG", long_options, &longindex)) != -1) {
        switch (opt) {
        case 'x':
            settings.width = atoi(optarg);
            break;
        case 'y':
            settings.height = atoi(optarg);
            break;
        case 'a':
            free(settings.address);
            settings.address = strdup(optarg);
            break;
        case 'p':
            settings.port = atol(optarg);
            break;
        case 'f':
            settings.fps = atoi(optarg);
            break;
        case 'V':
            settings.no_video = 1;
            break;
        case 'G':
            settings.no_gui = 1;
            break;
        case 'S':
            // ???
            // settings.no_service = 1;
            break;
        case 'n':
            settings.vsync = false;
            break;
        case 'v':
            log_set_level(Debug);
            break;
        case 'b':
            free(settings.video_backend);
            settings.video_backend = strdup(optarg);
            break;
        case 'u':
            free(settings.ui_backend);
            settings.ui_backend = strdup(optarg);
            break;
        case 'c':
            DBG("Loading config file %s...", optarg);
            if ((ret = settings_load_file(&settings, optarg)) != 0) {
                WARN("Config load failed: %d", ret);
            }
            break;
        case 's':
            // settings.save_settings = 1;
            // _settingspath = strdup(optarg);
            break;
        case 'h':
            print_usage();
            return 1;
        default:
            WARN("Unknown option: %c", opt);
            print_usage();
            return 1;
        }
    }

    return 0;
}

int main(int argc, char* argv[])
{
    int ret;

    log_init();
    INFO("Starting up...");

    using_cli = !getenv("LS_SERVICE_NAMES");

    settings_init(&settings);

    if (using_cli) {
        INFO("Running via CLI");
    } else {
        INFO("Running as a service");
        settings_load_file(&settings, "/media/developer/apps/usr/palm/services/org.webosbrew.piccap.service/config.json");
    }

    if ((ret = parse_options(argc, argv)) != 0) {
        return ret;
    }

    signal(SIGINT, int_handler);
    loop = g_main_loop_new(NULL, false);

    if ((ret = service_init(&service, &settings)) != 0) {
        ERR("Service init failed: %d", ret);
        return -1;
    }

    if ((ret = service_register(&service, loop)) != 0) {
        WARN("Service register failed: %d", ret);
    }

    if (using_cli) {
        if ((ret = service_start(&service)) != 0) {
            ERR("Service startup failed: %d", ret);
            g_main_loop_quit(loop);
        }
    }

    DBG("Going into main loop..");

    g_main_loop_run(loop);

    DBG("Main loop quit...");

    DBG("Cleaning up service...");
    service_destroy(&service);

    g_main_loop_unref(loop);

    DBG("Service main finished");
    return 0;
}
