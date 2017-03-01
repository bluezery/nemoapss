#include <unistd.h>
#include <getopt.h>
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

#include <nemotool.h>
#include <nemotimer.h>
#include <nemoshow.h>

#include "nemoutil.h"
#include "widgets.h"
#include "nemoui.h"
#include "sound.h"

#define PLANET_GRAB_MIN_TIME 500
#define MENU_DESTROY_TIMEOUT 6000

typedef struct _ConfigMenuItem ConfigMenuItem;
struct _ConfigMenuItem {
    char *type;
    char *bg;
    char *txt;
    char *icon;
    char *exec;
    bool resize;
    double sxy;
};

static ConfigMenuItem *parse_tag_menu(XmlTag *tag)
{
    List *ll;
    XmlAttr *attr;
    ConfigMenuItem *item = calloc(sizeof(ConfigMenuItem), 1);
    item->sxy = 1.0;
    item->resize = true;

    LIST_FOR_EACH(tag->attrs, ll, attr) {
        if (!strcmp(attr->key, "type")) {
            if (attr->val) {
                 item->type = strdup(attr->val);
            }
        } else if (!strcmp(attr->key, "bg")) {
            if (attr->val) {
                 item->bg = strdup(attr->val);
            }
        } else if (!strcmp(attr->key, "txt")) {
            if (attr->val) {
                 item->txt = strdup(attr->val);
            }
        } else if (!strcmp(attr->key, "icon")) {
            if (attr->val) {
                 item->icon = strdup(attr->val);
            }
        } else if (!strcmp(attr->key, "exec")) {
            if (attr->val) {
                item->exec = strdup(attr->val);
            }
        } else if (!strcmp(attr->key, "sxy")) {
            if (attr->val) {
                item->sxy = atof(attr->val);
            }
        } else if (!strcmp(attr->key, "resize")) {
            if (attr->val) {
                if (!strcmp(attr->val, "on")) {
                    item->resize = true;
                } else {
                    item->resize = false;
                }
            }
        }
    }
    return item;
}

typedef struct _ConfigApp ConfigApp;
struct _ConfigApp {
    Config *config;
    List *menu_items;
    double sxy;
    char *planet_uri;
};

typedef struct _MenuView MenuView;
typedef struct _MenuMainItem MenuMainItem;

struct _MenuView {
    const char *uuid;
    ConfigApp *app;
    int width, height;
    struct nemotool *tool;
    struct nemoshow *show;

    NemoWidget *widget;
    struct showone *group;

    struct showone *planet;
    struct nemotimer *planet_timer;
    struct nemotimer *timer;

    List *items;
    struct nemotimer *destroy_timer;
};

struct _MenuMainItem {
    int w, h;
    MenuView *view;
    char *id;
    struct showone *group;
    struct showone *one;
};


void menu_item_down(MenuMainItem *it)
{
    _nemoshow_item_motion_bounce(it->one, NEMOEASE_CUBIC_INOUT_TYPE, 350, 0,
            "sx", 0.75, 0.85, "sy", 0.75, 0.85, NULL);
    _nemoshow_item_motion_bounce(it->one, NEMOEASE_CUBIC_INOUT_TYPE, 350, 0,
            "sx", 0.75, 0.85, "sy", 0.75, 0.85, NULL);
}

void menu_item_up(MenuMainItem *it)
{
    _nemoshow_item_motion(it->one, NEMOEASE_CUBIC_INOUT_TYPE, 250, 0,
            "sx", 1.0, "sy", 1.0, NULL);
    _nemoshow_item_motion(it->one, NEMOEASE_CUBIC_INOUT_TYPE, 250, 0,
            "sx", 1.0, "sy", 1.0, NULL);
}

static void _menu_item_grab_event(NemoWidgetGrab *grab, NemoWidget *widget, struct showevent *event, void *userdata)
{
    MenuMainItem *it = userdata;
    MenuView *view = it->view;
    nemotimer_set_timeout(view->destroy_timer, MENU_DESTROY_TIMEOUT);

    struct nemoshow *show = nemowidget_get_show(widget);
    double ex, ey;

    nemowidget_transform_from_global(widget,
            nemoshow_event_get_x(event),
            nemoshow_event_get_y(event), &ex, &ey);

    if (nemoshow_event_is_down(show, event)) {
        nemotimer_set_timeout(view->timer, 0);
        List *l;
        MenuMainItem *itt;
        LIST_FOR_EACH(view->items, l, itt) {
            nemoshow_revoke_transition_one(view->show, itt->one, "ro");
        }
        menu_item_down(it);
    } else if (nemoshow_event_is_up(show, event)) {
        menu_item_up(it);
        nemotimer_set_timeout(view->timer, 100);
#if 0
        if (nemoshow_event_is_single_click(show, event)) {
            MenuView *view = it->view;
            char path[PATH_MAX];
            char name[PATH_MAX];
            char args[PATH_MAX];
            char *buf;
            char *tok;
            buf = strdup(it->menu_item->exec);
            tok = strtok(buf, ";");
            snprintf(name, PATH_MAX, "%s", tok);
            snprintf(path, PATH_MAX, "%s", tok);
            tok = strtok(NULL, "");
            snprintf(args, PATH_MAX, "%s", tok);
            free(buf);

            nemo_execute(view->uuid, it->menu_item->type, path, args, it->menu_item->resize? "on" : "off",
                    ex, ey, 0.0, it->menu_item->sxy, it->menu_item->sxy);
        }
#endif
    }
    nemoshow_dispatch_frame(show);
}

static void _menu_view_grab_event(NemoWidgetGrab *grab, NemoWidget *widget, struct showevent *event, void *userdata)
{
    MenuView *view = userdata;
    nemotimer_set_timeout(view->destroy_timer, MENU_DESTROY_TIMEOUT);
}

void menu_item_show(MenuMainItem *it, uint32_t easetype, int duration, int delay)
{
    if (duration > 0) {
        _nemoshow_item_motion(it->group, easetype, duration, delay,
                "alpha", 1.0, NULL);
        _nemoshow_item_motion(it->one, easetype, duration, delay,
                "sx", 1.0, "sy", 1.0, NULL);
    } else {
        nemoshow_item_set_alpha(it->group, 1.0);
        nemoshow_item_scale(it->one, 1.0, 1.0);
    }
}

void menu_item_hide(MenuMainItem *it, uint32_t easetype, int duration, int delay)
{
    if (duration > 0) {
        _nemoshow_item_motion(it->group, easetype, duration, delay,
                "alpha", 0.0, NULL);
        _nemoshow_item_motion(it->one, easetype, duration, delay,
                "alpha", 0.0, "sx", 0.0, "sy", 0.0,
                NULL);
    } else {
        nemoshow_item_set_alpha(it->group, 0.0);
        nemoshow_item_scale(it->one, 0.0, 0.0);
    }
}

void menu_item_rotate(MenuMainItem *it, uint32_t easetype, int duration, int delay)
{
    _nemoshow_item_motion(it->one, easetype, duration, delay,
            "ro", 360.0, NULL);
}

void menu_item_destroy(MenuMainItem *it)
{
    free(it->id);
    nemoshow_one_destroy(it->one);
    nemoshow_one_destroy(it->group);
    free(it);
}

MenuMainItem *menu_create_item(MenuView *view, const char *id)
{
    MenuMainItem *it = calloc(sizeof(MenuMainItem), 1);
    it->view = view;
    it->id = strdup(id);

    char uri[PATH_MAX];
    snprintf(uri, PATH_MAX, UNIVERSE_IMG_DIR"/menu/main/%s.png", id);

    struct showone *group;
    struct showone *one;
    it->group = group = GROUP_CREATE(view->group);
    nemoshow_item_set_alpha(group, 0.0);

    file_get_image_wh(uri, &it->w, &it->h);
    it->w *= view->app->sxy;
    it->h *= view->app->sxy;
    it->one = one = IMAGE_CREATE(group, it->w, it->h, uri);
    nemoshow_item_set_anchor(one, 0.5, 0.5);
    nemoshow_item_translate(one, it->w/2, it->h/2);
    nemoshow_item_scale(one, 0.0, 0.0);

    view->items = list_append(view->items, it);
    return it;
}

void menu_view_destroy(MenuView *view)
{
    MenuMainItem *it;
    LIST_FREE(view->items, it) menu_item_destroy(it);
    nemoshow_one_destroy(view->group);
    free(view);
}

void menu_view_hide(MenuView *view)
{
    nemotimer_set_timeout(view->destroy_timer, 0);
    nemowidget_set_alpha(view->widget, NEMOEASE_CUBIC_INOUT_TYPE, 500, 0, 0.0);

    nemotimer_set_timeout(view->timer, 0);
    nemotimer_set_timeout(view->planet_timer, 0);
    _nemoshow_item_motion(view->planet, NEMOEASE_CUBIC_INOUT_TYPE, 500, 0,
            "alpha", 0.0, "sx", 0.0, "sy", 0.0, NULL);

    int delay = 0;
    List *l;
    MenuMainItem *it;
    LIST_FOR_EACH(view->items, l, it) {
        menu_item_hide(it, NEMOEASE_CUBIC_INOUT_TYPE, 500, delay);
        delay += 50;
    }
    nemoshow_dispatch_frame(view->show);
}

void _menu_view_destroy(struct nemotimer *timer, void *userdata)
{
    MenuView *view = userdata;
    menu_view_destroy(view);
}

void _menu_destroy_timeout(struct nemotimer *timer, void *userdata)
{
    MenuView *view = userdata;
    menu_view_hide(view);
    TOOL_ADD_TIMER(view->tool, 1000, _menu_view_destroy, view);
}

static void _menu_view_event(NemoWidget *widget, const char *id, void *info, void *userdata)
{
    struct showevent *event = info;
    struct nemoshow *show = nemowidget_get_show(widget);
    MenuView *view = userdata;

    double ex, ey;
    nemowidget_transform_from_global(widget,
            nemoshow_event_get_x(event),
            nemoshow_event_get_y(event), &ex, &ey);

    if (nemoshow_event_is_down(show, event)) {
        struct showone *one;
        one = nemowidget_pick_one(widget, ex, ey);
        if (one) {
            MenuMainItem *it = nemoshow_one_get_userdata(one);
            if (it) {
                nemowidget_create_grab(widget, event,
                        _menu_item_grab_event, it);
            } else {
                nemowidget_create_grab(widget, event,
                        _menu_view_grab_event, view);
            }
        } else {
            struct nemotool *tool = nemowidget_get_tool(widget);
            uint64_t device = nemoshow_event_get_device(event);
            float vx, vy;
            nemoshow_transform_to_viewport(show, ex, ey, &vx, &vy);
            nemotool_touch_bypass(tool, device, vx, vy);
        }
    }
}

void menu_view_show(MenuView *view, uint32_t easetype, int duration, int delay)
{
    nemowidget_show(view->widget, 0, 0, 0);
    nemowidget_set_alpha(view->widget, easetype, duration, delay, 1.0);

    nemotimer_set_timeout(view->timer, 100);
    nemotimer_set_timeout(view->planet_timer, 100);
    _nemoshow_item_motion(view->planet, easetype, duration, delay,
            "alpha", 1.0, "sx", 1.0, "sy", 1.0, NULL);
    int _delay = 0;
    List *l;
    MenuMainItem *it;
    LIST_FOR_EACH(view->items, l, it) {
        menu_item_show(it, NEMOEASE_CUBIC_INOUT_TYPE, 1000, _delay + delay);
        _delay += 150;
    }

    nemoshow_dispatch_frame(view->show);
    nemotimer_set_timeout(view->destroy_timer, MENU_DESTROY_TIMEOUT);
}

static void _menu_view_timeout(struct nemotimer *timer, void *userdata)
{
    MenuView *view = userdata;

    int duration = 0;
    List *l;
    MenuMainItem *it;
    LIST_FOR_EACH(view->items, l, it) {
        double ro = nemoshow_item_get_rotate(it->one);
        if (ceil(ro) == 360) {
            ro = 0.0;
            nemoshow_item_rotate(it->one, 0.0);
        }
        duration = 40000 * ((360 - ro)/360.0);
        menu_item_rotate(it, NEMOEASE_LINEAR_TYPE, duration, 0);
    }
    nemoshow_dispatch_frame(view->show);
    nemotimer_set_timeout(timer, duration);
}

static void _menu_view_planet_timeout(struct nemotimer *timer, void *userdata)
{
    MenuView *view = userdata;
    int duration = 15000;
    nemoshow_item_rotate(view->planet, 0.0);
    _nemoshow_item_motion(view->planet, NEMOEASE_LINEAR_TYPE, duration, 0,
            "ro", -360.0, NULL);
    nemotimer_set_timeout(timer, duration);
}

MenuView *menu_view_create(NemoWidget *parent, int width, int height, ConfigApp *app)
{
    MenuView *view = calloc(sizeof(MenuView), 1);
    view->tool = nemowidget_get_tool(parent);
    view->show = nemowidget_get_show(parent);
    view->uuid = nemowidget_get_uuid(parent);
    view->width = width;
    view->height = height;
    view->app = app;

    NemoWidget *widget;
    struct showone *group;
    view->widget = widget = nemowidget_create_vector(parent, width, height);
    nemowidget_append_callback(widget, "event", _menu_view_event, view);
    nemowidget_set_alpha(widget, 0, 0, 0, 0.0);

    view->group = group = GROUP_CREATE(nemowidget_get_canvas(widget));

    struct showone *one;
    int w, h;
    file_get_image_wh(app->planet_uri, &w, &h);
    w *= app->sxy;
    h *= app->sxy;
    view->planet = one = IMAGE_CREATE(group, w, h, app->planet_uri);
    nemoshow_one_set_state(one, NEMOSHOW_PICK_STATE);
    nemoshow_item_set_anchor(one, 0.5, 0.5);
    nemoshow_item_translate(one, width/2, height/2);
    nemoshow_item_set_alpha(one, 0.0);
    nemoshow_item_scale(one, 0.0, 0.0);

    menu_create_item(view, "content");
    menu_create_item(view, "entertainment");
    menu_create_item(view, "internet");
    menu_create_item(view, "setting");
    menu_create_item(view, "utility");

#if 0
    int i = 1;
    List *l;
    ConfigMenuItem *itt;
    LIST_FOR_EACH(app->menu_items, l, itt) {
        MenuMainItem *it = menu_create_item(view, itt, uri);
        view->items = list_append(view->items, it);
        i++;
    }
#endif

    view->timer = TOOL_ADD_TIMER(view->tool, 0, _menu_view_timeout, view);
    view->planet_timer = TOOL_ADD_TIMER(view->tool, 0, _menu_view_planet_timeout, view);
    view->destroy_timer = TOOL_ADD_TIMER(view->tool, 0, _menu_destroy_timeout, view);

    return view;
}

static ConfigApp *_config_load(const char *domain, const char *appname, const char *filename, int argc, char *argv[])
{
    ConfigApp *app = calloc(sizeof(ConfigApp), 1);
    app->config = config_load(domain, appname, filename, argc, argv);

    Xml *xml;
    if (app->config->path) {
        xml = xml_load_from_path(app->config->path);
        if (!xml) ERR("Load configuration failed: %s", app->config->path);
    } else {
        xml = xml_load_from_domain(domain, filename);
        if (!xml) ERR("Load configuration failed: %s:%s", domain, filename);
    }
    if (!xml) {
        config_unload(app->config);
        free(app);
        return NULL;
    }

    char buf[PATH_MAX];
    const char *temp;

    int width, height;
    snprintf(buf, PATH_MAX, "%s/size", appname);
    temp = xml_get_value(xml, buf, "width");
    if (!temp) {
        ERR("No size width in %s", appname);
    } else {
        width = atoi(temp);
    }
    temp = xml_get_value(xml, buf, "height");
    if (!temp) {
        ERR("No size height in %s", appname);
    } else {
        height = atoi(temp);
    }

    double sx = 1.0;
    double sy = 1.0;
    if (width > 0) sx = (double)app->config->width/width;
    if (width > 0) sy = (double)app->config->height/height;
    if (sx > sy) app->sxy = sy;
    else app->sxy = sx;

    snprintf(buf, PATH_MAX, "%s/items", appname);
    List *tags  = xml_search_tags(xml, buf);
    List *l;
    XmlTag *tag;
    LIST_FOR_EACH(tags, l, tag) {
        ConfigMenuItem *it = parse_tag_menu(tag);
        if (!it) continue;
        if (!it->type) it->type = strdup("app");
        app->menu_items = list_append(app->menu_items, it);
    }

    xml_unload(xml);

    struct option options[] = {
        {"planet", required_argument, NULL, 'p'},
        { NULL }
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "p:", options, NULL)) != -1) {
        switch(opt) {
            case 'p':
                app->planet_uri = strdup(optarg);
                break;
            default:
                break;
        }
    }
    RET_IF(!app->planet_uri, NULL);

    return app;
}

static void _config_unload(ConfigApp *app)
{
    config_unload(app->config);
    free(app->planet_uri);
    free(app);
}

int main(int argc, char *argv[])
{
    ConfigApp *app = _config_load(PROJECT_NAME, APPNAME, CONFXML, argc, argv);
    RET_IF(!app, -1);

    struct nemotool *tool = TOOL_CREATE();
    NemoWidget *win = nemowidget_create_win_base(tool, APPNAME, app->config);
    nemowidget_win_enable_scale(win, -1);
    nemowidget_win_enable_rotate(win, -1);

    MenuView *view = menu_view_create(win, app->config->width, app->config->height, app);
    menu_view_show(view, NEMOEASE_CUBIC_INOUT_TYPE, 1000, 0);

    nemowidget_show(win, 0, 0, 0);
    nemotool_run(tool);

    nemowidget_destroy(win);
    TOOL_DESTROY(tool);
    _config_unload(app);

    return 0;
}
