#include <unistd.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <libgen.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/timerfd.h>
#include <getopt.h>

#include <nemotool.h>
#include <nemotimer.h>
#include <nemoshow.h>

#include "xemoutil.h"
#include "widgets.h"
#include "nemoui.h"
#include "sound.h"

typedef struct _ConfigApp ConfigApp;
struct _ConfigApp {
    Config *config;
    double sxy;
    char *bgpath;
};

typedef struct _StarDot StarDot;
struct _StarDot {
    struct nemotool *tool;
    struct showone *parent;
    uint32_t color;
    struct showone *blur;
    struct showone *group;
    struct showone *one;

    struct nemotimer *timer;
    List *ones;
    int idx;
};

StarDot *star_dot_create(struct nemotool *tool, struct showone *parent, int r, uint32_t color)
{
    StarDot *dot = (StarDot *)malloc(sizeof(StarDot));
    dot->tool = tool;
    dot->parent = parent;
    dot->color = color;
    dot->blur = BLUR_CREATE("solid", 5);
    dot->ones = NULL;
    dot->idx = 0;

    struct showone *group;
    dot->group = group = GROUP_CREATE(parent);
    nemoshow_item_set_alpha(group, 0.0);

    struct showone *one;
    one = dot->one = CIRCLE_CREATE(group, r * 1.5);
    nemoshow_item_set_alpha(one, 0.8);
    nemoshow_item_set_fill_color(one, RGBA(WHITE));
    nemoshow_item_set_filter(one, dot->blur);

    return dot;
}

void star_dot_show(StarDot *dot, uint32_t easetype, int duration, int delay)
{
    _nemoshow_item_motion(dot->group, easetype, duration, delay,
            "alpha", 1.0, NULL);
}

void star_dot_hide(StarDot *dot, uint32_t easetype, int duration, int delay)
{
    _nemoshow_item_motion(dot->group, easetype, duration, delay,
            "alpha", 0.0, NULL);
}

void star_dot_destroy(StarDot *dot)
{
    nemoshow_one_destroy(dot->blur);
    nemoshow_one_destroy(dot->group);
    free(dot);
}

void _star_dot_destroy(struct nemotimer *timer, void *userdata)
{
    StarDot *dot = (StarDot *)userdata;
    star_dot_destroy(dot);
    nemotimer_destroy(timer);
}

void star_dot_hide_destroy(StarDot *dot, uint32_t easetype, int duration, int delay)
{
    star_dot_hide(dot, easetype, duration, delay);
    if (duration > 0) {
        TOOL_ADD_TIMER(dot->tool, duration + delay, _star_dot_destroy, dot);
    } else {
        star_dot_destroy(dot);
    }
}

void star_dot_translate(StarDot *dot, int tx, int ty)
{
    nemoshow_item_translate(dot->group, tx, ty);
}

typedef struct _StarSign StarSign;

struct _StarSign {
    struct nemotool *tool;
    struct nemoshow *show;
    struct showone *group;
    List *dots;
    PolyLine *pl;

    uint32_t prev_time;
    double prev_x, prev_y;
    struct nemotimer *timer;
    struct nemotimer *destroy_timer;
    struct nemotimer *hide_timer;
};

static void _star_sign_timeout(struct nemotimer *timer, void *userdata)
{
    StarSign *sign = userdata;
    List *l;
    StarDot *dot;

    int duration = 1000;
    LIST_FOR_EACH(sign->dots, l, dot) {
        double rad = rand()%20 * 0.1 + 0.5;
        double r = rad * 3;
        double sx = rad;
        double sy = rad;
        _nemoshow_item_motion(dot->blur, NEMOEASE_LINEAR_TYPE, duration, rad * 300,
                "r", r, NULL);
        _nemoshow_item_motion(dot->one, NEMOEASE_LINEAR_TYPE, duration, rad * 300,
                "sx", sx, "sy", sy, NULL);
    }
    nemotimer_set_timeout(timer, duration + 3000);
}

void star_sign_destroy(StarSign *sign)
{
    StarDot *dot;
    LIST_FREE(sign->dots, dot) star_dot_destroy(dot);
    polyline_destroy(sign->pl);
    nemoshow_one_destroy(sign->group);
    nemotimer_destroy(sign->timer);
    nemotimer_destroy(sign->destroy_timer);
    free(sign);
}

void _star_sign_destroy_timeout(struct nemotimer *timer, void *userdata)
{
    StarSign *sign = userdata;
    star_sign_destroy(sign);
}

void star_sign_hide_destroy(StarSign *sign)
{
    StarDot *dot;
    int i = 0;
    LIST_FREE(sign->dots, dot) {
        star_dot_hide_destroy(dot, NEMOEASE_CUBIC_INOUT_TYPE, 500, 500 * i);
        i++;
    }
    i = 0;
    List *l;
    struct showone *one;
    LIST_FOR_EACH(sign->pl->ones, l, one) {
        _nemoshow_item_motion(one, NEMOEASE_CUBIC_INOUT_TYPE, 500, 500 * i,
                "alpha", 0.0, NULL);
        i++;
    }
    nemotimer_set_timeout(sign->destroy_timer, 500 + 500 * i);
}

void _star_sign_hide_timeout(struct nemotimer *timer, void *userdata)
{
    StarSign *sign = userdata;
    star_sign_hide_destroy(sign);
}

StarSign *star_sign_create(struct nemotool *tool, struct showone *parent, uint32_t color, int width)
{
    StarSign *sign = calloc(sizeof(StarSign), 1);
    sign->tool = tool;
    sign->group = GROUP_CREATE(parent);
    sign->timer = TOOL_ADD_TIMER(tool, 500, _star_sign_timeout, sign);
    sign->pl = polyline_create(tool, sign->group, WHITE, 2);
    sign->destroy_timer = TOOL_ADD_TIMER(tool, 0, _star_sign_destroy_timeout, sign);
    sign->hide_timer = TOOL_ADD_TIMER(tool, 0, _star_sign_hide_timeout, sign);
    return sign;
}

void star_sign_add_dot(StarSign *sign, uint32_t time, double ex, double ey)
{
    if (((time - sign->prev_time) > 200) &&
            (DISTANCE(ex, ey, sign->prev_x, sign->prev_y) > 50)) {
        sign->prev_time = time;
        int dx = rand()%60 - 30;
        int dy = rand()%60 - 30;
        sign->prev_x = ex + dx;
        sign->prev_y = ey + dy;

        StarDot *dot = star_dot_create(sign->tool, sign->group, 5, 0x24D6D6FF);
        star_dot_translate(dot, ex + dx, ey + dy);
        star_dot_show(dot, NEMOEASE_CUBIC_INOUT_TYPE, 1000, 0);
        sign->dots = list_append(sign->dots, dot);

        polyline_add_dot_custom(sign->pl, ex + dx, ey + dy, 0.5, WHITE);
    }
}

void star_sign_start(StarSign *sign, uint32_t time, double ex, double ey)
{
    sign->prev_time = time;
    sign->prev_time = time;
    int dx = rand()%60 - 30;
    int dy = rand()%60 - 30;
    sign->prev_x = ex + dx;
    sign->prev_y = ey + dy;

    StarDot *dot = star_dot_create(sign->tool, sign->group, 5, 0x24D6D6FF);
    star_dot_translate(dot, ex + dx, ey + dy);
    star_dot_show(dot, NEMOEASE_CUBIC_INOUT_TYPE, 1000, 0);
    sign->dots = list_append(sign->dots, dot);

    polyline_add_dot_custom(sign->pl, ex + dx, ey + dy, 0.5, WHITE);
}

void star_sign_stop(StarSign *sign)
{
    nemotimer_set_timeout(sign->hide_timer, 1000);
}

typedef struct _Background Background;
typedef struct _BackgroundView BackgroundView;

struct _Background {
    int width, height;
    struct nemotool *tool;
    struct nemoshow *show;
    int framerate;

    double ro;
    NemoWidget *widget;
    struct showone *group;
    struct showone *bg;
};

static void _background_grab_event(NemoWidgetGrab *grab, NemoWidget *widget, struct showevent *event, void *userdata)
{
    StarSign *sign = userdata;
    struct nemoshow *show = nemowidget_get_show(widget);
    double ex, ey;
    nemowidget_transform_from_global(widget,
            nemoshow_event_get_x(event),
            nemoshow_event_get_y(event), &ex, &ey);
    uint32_t time = nemoshow_event_get_time(event);

    if (nemoshow_event_is_down(show, event)) {
        star_sign_start(sign, time, ex, ey);
    } else if (nemoshow_event_is_motion(show, event)) {
        star_sign_add_dot(sign, time, ex, ey);
    } else if (nemoshow_event_is_up(show, event)) {
        star_sign_add_dot(sign, time, ex, ey);
        star_sign_stop(sign);
    }
    nemoshow_dispatch_frame(show);
}

static void _background_event(NemoWidget *widget, const char *id, void *info, void *userdata)
{
    struct showevent *event = info;
    struct nemoshow *show = nemowidget_get_show(widget);
    Background *bg = userdata;

    double ex, ey;
    nemowidget_transform_from_global(widget,
            nemoshow_event_get_x(event),
            nemoshow_event_get_y(event), &ex, &ey);

    if (nemoshow_event_is_down(show, event)) {
        struct nemotool *tool = nemowidget_get_tool(widget);
        StarSign *sign = star_sign_create(tool, bg->group, WHITE, 2);
        nemowidget_create_grab(widget, event,
                _background_grab_event, sign);
    }
}

Background *background_create(NemoWidget *parent, int width, int height, double sxy, char *bgpath, int framerate)
{
    Background *bg = calloc(sizeof(Background), 1);
    bg->tool = nemowidget_get_tool(parent);
    bg->show = nemowidget_get_show(parent);
    bg->width = width;
    bg->height = height;
    bg->framerate = framerate;

    NemoWidget *widget;
    struct showone *group;
    struct showone *one;
    bg->widget = widget = nemowidget_create_vector(parent, width, height);
    nemowidget_pivot(widget, 0.5, 0.5);
    nemowidget_append_callback(widget, "event", _background_event, bg);
    nemowidget_set_alpha(widget, 0, 0, 0, 0.0);

    bg->group = group = GROUP_CREATE(nemowidget_get_canvas(widget));

    if (bgpath) {
        bg->bg = one = IMAGE_CREATE(group, width, height, bgpath);
        nemoshow_item_set_anchor(one, 0.5, 0.5);
        nemoshow_item_translate(one, width/3, height/2);
    } else if (file_is_exist(APPDATA_ROOT"/back.png")) {
        bg->bg = one = IMAGE_CREATE(group, width, height, APPDATA_ROOT"/back.png");
        nemoshow_item_set_anchor(one, 0.5, 0.5);
        nemoshow_item_translate(one, width/2, height/2);
    }

    return bg;
}

void background_translate(Background *bg, double tx, double ty)
{
    nemowidget_translate(bg->widget, 0, 0, 0, tx, ty);
}

void background_show(Background *bg, uint32_t easetype, int duration, int delay)
{
    nemowidget_show(bg->widget, 0, 0, 0);
    nemowidget_set_alpha(bg->widget, easetype, duration, delay, 1.0);

    nemoshow_dispatch_frame(bg->show);
}

struct _BackgroundView {
    int width, height;
    struct nemotool *tool;
    struct nemoshow *show;

    Background *bg;
};

BackgroundView *background_view_create(NemoWidget *parent, int width, int height, ConfigApp *app)
{
    BackgroundView *view = calloc(sizeof(BackgroundView), 1);
    view->tool = nemowidget_get_tool(parent);
    view->show = nemowidget_get_show(parent);
    view->width = width;
    view->height = height;

    Background *bg;
    view->bg = bg = background_create(parent, width, height, app->config->sxy, app->bgpath, app->config->framerate);

    return view;
}

void background_view_show(BackgroundView *view, uint32_t easetype, int duration, int delay)
{
    background_show(view->bg, NEMOEASE_CUBIC_INOUT_TYPE, 1000, 0);
}

static ConfigApp *_config_load(const char *domain, const char *filename, int argc, char *argv[])
{
    ConfigApp *app = calloc(sizeof(ConfigApp), 1);
    app->config = config_load(domain, filename, argc, argv);

    struct option options[] = {
        {"file", required_argument, NULL, 'f'},
        { NULL }
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "f:", options, NULL)) != -1) {
        switch(opt) {
            case 'f':
                app->bgpath = strdup(optarg);
                break;
            default:
                break;
        }
    }

    return app;
}

static void _config_unload(ConfigApp *app)
{
    config_unload(app->config);
    if (app->bgpath) free(app->bgpath);
    free(app);
}

int main(int argc, char *argv[])
{
    ConfigApp *app = _config_load(PROJECT_NAME, CONFXML, argc, argv);
    RET_IF(!app, -1);

    struct nemotool *tool = TOOL_CREATE();
    NemoWidget *win = nemowidget_create_win_config(tool, APPNAME, app->config);

    BackgroundView *view = background_view_create(win, app->config->width, app->config->height, app);
    background_view_show(view, NEMOEASE_CUBIC_INOUT_TYPE, 1000, 0);

    nemowidget_show(win, 0, 0, 0);
    nemotool_run(tool);

    nemowidget_destroy(win);
    TOOL_DESTROY(tool);
    _config_unload(app);

    return 0;
}
