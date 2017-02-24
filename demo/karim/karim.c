#include <unistd.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <getopt.h>
#include <libgen.h>
#include <time.h>

#include <nemotool.h>
#include <nemotimer.h>
#include <nemoshow.h>

#include "nemoutil.h"
#include "widgets.h"
#include "nemoui.h"

#define SAVER_TIMEOUT 600000

typedef enum
{
    KARIM_TYPE_NONE = 0,
    KARIM_TYPE_MENU,
    KARIM_TYPE_INTRO,
    KARIM_TYPE_REGION,
    KARIM_TYPE_WORK,
    KARIM_TYPE_YEAR,
    KARIM_TYPE_HONEY,
    KARIM_TYPE_VIEWER,
} KarimType;

typedef struct _IntroView IntroView;
typedef struct _RegionView RegionView;
typedef struct _WorkView WorkView;
typedef struct _YearView YearView;
typedef struct _MenuView MenuView;
typedef struct _HoneyView HoneyView;
typedef struct _ViewerView ViewerView;
typedef struct _SaverView SaverView;
typedef struct _Karim Karim;

struct _Karim
{
    int w, h;
    struct nemotool *tool;
    struct nemoshow *show;
    const char *uuid;
    NemoWidget *parent;

    NemoWidget *widget;
    struct showone *bg;

    KarimType type;
    IntroView *intro;
    RegionView *region;
    WorkView *work;
    YearView *year;
    MenuView *menu;
    HoneyView *honey;
    ViewerView *viewer;
    SaverView *saver;

    struct nemotimer *saver_timer;
    NemoWidget *event_widget;
};

typedef enum {
    VIEWER_MODE_INTRO = 0,
    VIEWER_MODE_GALLERY,
} ViewerMode;

typedef struct _ViewerItem ViewerItem;
struct _ViewerItem {
    ViewerView *view;
    NemoWidget *widget0, *widget;
    struct showone *clip;
    Image *img0, *img1;
    struct showone *event;

    char *url;
    struct showone *btn_group;
    struct showone *btn_bg;
    struct showone *btn;
};

struct _ViewerView {
    Karim *karim;
    int w, h;
    struct nemotool *tool;
    struct nemoshow *show;
    NemoWidget *widget;

    struct showone *group;
    struct showone *bg;

    NemoWidgetGrab *grab;
    ViewerMode mode;
    double gallery_x;
    List *items;

    NemoWidget *title_widget;
    struct showone *title_group;
    struct showone *title_bg;
    struct showone *title0;
    struct showone *title1;

    struct nemotimer *title_timer;
};

void viewer_item_translate(ViewerItem *item, uint32_t easetype, int duration, int delay, double tx, double ty)
{
    nemowidget_translate(item->widget0, easetype, duration, delay, tx, ty);
    nemowidget_translate(item->widget, easetype, duration, delay, tx, ty);
}

void viewer_item_down(ViewerItem *item, uint32_t easetype, int duration, int delay)
{
    nemowidget_set_alpha(item->widget0, easetype, duration, delay, 0.5);
}

void viewer_item_up(ViewerItem *item, uint32_t easetype, int duration, int delay)
{
    nemowidget_set_alpha(item->widget0, easetype, duration, delay, 1.0);
}

void viewer_item_mode(ViewerItem *item, uint32_t easetype, int duration, int delay, ViewerMode mode)
{
    if (mode == VIEWER_MODE_INTRO) {
        nemowidget_show(item->widget0, 0, 0, 0);
        nemowidget_hide(item->widget, 0, 0, 0);
        nemowidget_set_alpha(item->widget0, easetype, duration, delay, 1.0);
        nemowidget_set_alpha(item->widget, easetype, duration, delay, 0.0);
    } else if (mode == VIEWER_MODE_GALLERY) {
        nemowidget_hide(item->widget0, 0, 0, 0);
        nemowidget_show(item->widget, 0, 0, 0);
        nemowidget_set_alpha(item->widget0, easetype, duration, delay, 0.0);
        nemowidget_set_alpha(item->widget, easetype, duration, delay, 1.0);
    }
}

void viewer_view_mode(ViewerView *view, ViewerMode mode, ViewerItem *modeitem)
{
    if (view->mode == !!mode) return;

    view->mode = mode;
    if (mode == VIEWER_MODE_INTRO) {
        int cnt = list_count(view->items);
        double w = view->w/cnt;
        view->gallery_x = -(cnt - 1) * (w/2);
        int i = 0;

        List *l;
        ViewerItem *item;
        LIST_FOR_EACH(view->items, l, item) {
            viewer_item_mode(item, NEMOEASE_CUBIC_OUT_TYPE, 1000, 0, mode);
            viewer_item_translate(item, NEMOEASE_CUBIC_OUT_TYPE, 1000, 0,
                    view->gallery_x + w * i, 0);
            i++;
        }
    } else if (mode == VIEWER_MODE_GALLERY) {
        int id = list_get_idx(view->items, modeitem);
        view->gallery_x = -view->w * ((int)id);

        int i = 0;
        List *l;
        ViewerItem *item;
        LIST_FOR_EACH(view->items, l, item) {
            viewer_item_mode(item, NEMOEASE_CUBIC_OUT_TYPE, 1500, 0, mode);
            viewer_item_translate(item, NEMOEASE_CUBIC_OUT_TYPE, 1000, 0,
                    view->gallery_x + view->w * i, 0);
            i++;
        }
    }
    nemoshow_dispatch_frame(view->show);
}

static void _viewer_item_btn_grab_event(NemoWidgetGrab *grab, NemoWidget *widget, struct showevent *event, void *userdata)
{
    struct nemoshow *show = nemowidget_get_show(widget);

    ViewerItem *item = userdata;
    ViewerView *view = item->view;

    if (nemoshow_event_is_down(show, event)) {
        _nemoshow_item_motion_bounce(item->btn_group, NEMOEASE_CUBIC_INOUT_TYPE, 500, 0,
                "sx", 1.5, 1.25, "sy", 1.5, 1.25, NULL);
        nemoshow_dispatch_frame(show);
    } else if (nemoshow_event_is_up(show, event)) {
        view->grab = NULL;
        _nemoshow_item_motion_bounce(item->btn_group, NEMOEASE_CUBIC_INOUT_TYPE, 500, 0,
                "sx", 0.8, 1.0, "sy", 0.8, 1.0, NULL);

        if (nemoshow_event_is_single_click(show, event)) {
            char path[PATH_MAX];
            char name[PATH_MAX];
            char args[PATH_MAX];

            snprintf(name, PATH_MAX, "/usr/bin/electron");
            snprintf(path, PATH_MAX, "%s", name);
            snprintf(args, PATH_MAX, "%s", item->url);

            ERR("(%s) (%s) (%s)", path, name, item->url);
            nemo_execute(view->karim->uuid, "xapp", path, args, "off",
                    view->w/2.0, view->h/2.0, 0, 1, 1);
        }
        nemoshow_dispatch_frame(show);
    }
}

static void _viewer_item_gallery_grab_event(NemoWidgetGrab *grab, NemoWidget *widget, struct showevent *event, void *userdata)
{
    struct nemoshow *show = nemowidget_get_show(widget);
    ViewerItem *item = userdata;
    ViewerView *view = item->view;

    double ex;
    nemowidget_transform_from_global(widget,
            nemoshow_event_get_x(event),
            nemoshow_event_get_y(event), &ex, NULL);
    double gx;
    nemowidget_transform_from_global(widget,
            nemoshow_event_get_grab_x(event),
            nemoshow_event_get_grab_y(event), &gx, NULL);

    if (nemoshow_event_is_motion(show, event)) {
        int i = 0;
        List *l;
        ViewerItem *item;
        LIST_FOR_EACH(view->items, l, item) {
            viewer_item_translate(item, 0, 0, 0,
                    view->gallery_x + view->w * i + ex - gx, 0);
            i++;
        }
        nemoshow_dispatch_frame(view->show);
    } else if (nemoshow_event_is_up(show, event)) {
        view->grab = NULL;
        if (nemoshow_event_is_single_click(show, event)) {
            int rx = view->w/6;
            if (ex < rx || ex > (view->w - rx)) {
                viewer_view_mode(view, VIEWER_MODE_INTRO, NULL);
            }
        } else {
            int tx = ex - gx;
            if (tx > view->w/4) {
                view->gallery_x += view->w;
            } else if (tx < -view->w/4) {
                view->gallery_x -= view->w;
            }

            int cnt = list_count(view->items);
            if (view->gallery_x > 0) view->gallery_x = 0;
            else if (view->gallery_x < (double)-(cnt - 1) * view->w)
                view->gallery_x = -(cnt - 1) * view->w;

            int i = 0;
            List *l;
            ViewerItem *item;
            LIST_FOR_EACH(view->items, l, item) {
                viewer_item_translate(item, NEMOEASE_CUBIC_INOUT_TYPE, 500, 0,
                        view->gallery_x + view->w * i, 0);
                i++;
            }
            nemoshow_dispatch_frame(view->show);
        }
    }
}

static void _viewer_item_gallery_event(NemoWidget *widget, const char *id, void *info, void *userdata)
{
    struct showevent *event = info;
    struct nemoshow *show = nemowidget_get_show(widget);
    ViewerItem *item = userdata;
    ViewerView *view = item->view;

    if (nemoshow_event_is_down(show, event)) {
        struct showone *one;
        double ex, ey;
        nemowidget_transform_from_global(widget,
                nemoshow_event_get_x(event),
                nemoshow_event_get_y(event), &ex, &ey);
        one = nemowidget_pick_one(widget, ex, ey);

        if (!view->grab) {
            if (one) {
                view->grab = nemowidget_create_grab(widget, event,
                        _viewer_item_btn_grab_event, item);
            } else {
                view->grab = nemowidget_create_grab(widget, event,
                        _viewer_item_gallery_grab_event, item);
            }
        }
    }
}

static void _viewer_item_clip_grab_event(NemoWidgetGrab *grab, NemoWidget *widget, struct showevent *event, void *userdata)
{
    struct nemoshow *show = nemowidget_get_show(widget);

    ViewerItem *item = userdata;
    ViewerView *view = item->view;

    if (nemoshow_event_is_down(show, event)) {
        viewer_item_down(item, NEMOEASE_CUBIC_OUT_TYPE, 500, 0);
        nemoshow_dispatch_frame(show);
    } else if (nemoshow_event_is_up(show, event)) {
        view->grab = NULL;
        viewer_item_up(item, NEMOEASE_CUBIC_OUT_TYPE, 500, 0);
        if (nemoshow_event_is_single_click(show, event)) {
            viewer_view_mode(view, VIEWER_MODE_GALLERY, item);
        }
        nemoshow_dispatch_frame(show);
    }
}

static void _viewer_item_clip_event(NemoWidget *widget, const char *id, void *info, void *userdata)
{
    struct showevent *event = info;
    ViewerItem *item = userdata;
    ViewerView *view = item->view;
    struct nemoshow *show = nemowidget_get_show(widget);

    if (nemoshow_event_is_down(show, event)) {
        double ex, ey;
        nemowidget_transform_from_global(widget,
                nemoshow_event_get_x(event),
                nemoshow_event_get_y(event), &ex, &ey);
        struct showone *one;
        one = nemowidget_pick_one(widget, ex, ey);
        if (!view->grab && one) {
            view->grab = nemowidget_create_grab(widget, event,
                    _viewer_item_clip_grab_event, item);
        }
    }
}

ViewerItem *viewer_view_create_item(ViewerView *view, NemoWidget *parent,
        const char *uri, const char *url, double width)
{
    ViewerItem *item = calloc(sizeof(ViewerItem), 1);
    item->view = view;
    if (url) item->url = strdup(url);

    int w, h;
    file_get_image_wh(uri, &w, &h);
    _rect_ratio_fit(w, h, view->w, view->h, &w, &h);

    NemoWidget *widget;
    struct showone *canvas;
    item->widget0 = widget = nemowidget_create_vector(parent, w, h);
    nemowidget_append_callback(widget, "event", _viewer_item_clip_event, item);
    nemowidget_enable_event_repeat(widget, true);
    nemowidget_set_alpha(widget, 0, 0, 0, 0.0);
    canvas = nemowidget_get_canvas(widget);

    // XXX: design clip as center aligned
    struct showone *clip;
    item->clip = clip = PATH_CREATE(NULL);
    nemoshow_item_path_moveto(clip, (w - width)/2, 0);
    nemoshow_item_path_lineto(clip, (w - width)/2 + width, 0);
    nemoshow_item_path_lineto(clip, (w - width)/2 + width, view->h);
    nemoshow_item_path_lineto(clip, (w - width)/2, view->h);
    nemoshow_item_path_lineto(clip, (w - width)/2, 0);
    nemoshow_item_path_close(clip);

    struct showone *one;
    item->event = one = RECT_CREATE(canvas, width, view->h);
    nemoshow_item_set_alpha(one, 0.0);
    nemoshow_item_translate(one, view->w/2 - width/2, 0);
    nemoshow_one_set_state(one, NEMOSHOW_PICK_STATE);
    nemoshow_one_set_userdata(one, item);

    Image *img;
    item->img0 = img = image_create(canvas);
    image_load_fit(img, view->tool, uri, view->w, view->h, NULL, NULL);
    image_set_anchor(img, 0.5, 0.5);
    image_translate(img, 0, 0, 0, view->w/2, view->h/2);
    image_set_clip(img, clip);

    item->widget = widget = nemowidget_create_vector(parent, w, h);
    nemowidget_append_callback(widget, "event", _viewer_item_gallery_event, item);
    nemowidget_enable_event_repeat(widget, true);
    nemowidget_set_alpha(widget, 0, 0, 0, 0.0);
    canvas = nemowidget_get_canvas(widget);

    item->img1 = img = image_create(canvas);
    image_load_fit(img, view->tool, uri, view->w, view->h, NULL, NULL);

    if (url) {
        double sx, sy;
        double x, y;
        sx = view->w/1920.0;
        sy = view->h/1080.0;

        struct showone *group;
        item->btn_group = group = GROUP_CREATE(canvas);

        double ww, hh;
        const char *uri;
        uri = KARIM_ICON_DIR"/viewer/3d button-background.svg";
        svg_get_wh(uri, &ww, &hh);
        ww = ww * sx;
        hh = hh * sy;
        item->btn_bg = one = SVG_PATH_GROUP_CREATE(group, ww, hh, uri);
        nemoshow_item_set_anchor(one, 0.5, 0.5);
        nemoshow_one_set_state(one, NEMOSHOW_PICK_STATE);
        nemoshow_one_set_userdata(one, item);
        ERR("%p", one);

        x = view->w - ww;
        y = view->h - hh;
        nemoshow_item_translate(group, x, y);

        x = 0;
        y = 0;
        uri = KARIM_ICON_DIR"/viewer/3d button-3d.svg";
        svg_get_wh(uri, &ww, &hh);
        ww = ww * sx;
        hh = hh * sy;
        item->btn = one = SVG_PATH_GROUP_CREATE(group, ww, hh, uri);
        nemoshow_item_set_anchor(one, 0.5, 0.5);
    }

    return item;
}

static void _viewer_view_title_timeout(struct nemotimer *timer, void *userdata)
{
    ViewerView *view = userdata;
    uint32_t easetype = NEMOEASE_CUBIC_IN_TYPE;
    int duration = 1000;
    int delay = 0;
    _nemoshow_item_motion(view->title_group, easetype, duration, delay + 250 + 250,
            "tx", 0.0, "ty", 0.0,
            "alpha", 0.0, NULL);
    _nemoshow_item_motion(view->title0, easetype, duration, delay + 250,
            "sx", 0.0, "sy", 0.8, NULL);
    _nemoshow_item_motion(view->title1, easetype, duration, delay,
            "sx", 0.0, "sy", 0.8, NULL);
    nemoshow_dispatch_frame(view->show);
}

static ViewerView *viewer_view_create(Karim *karim, NemoWidget *parent, int width, int height)
{
    ViewerView *view = calloc(sizeof(ViewerView), 1);
    view->karim = karim;
    view->tool = nemowidget_get_tool(parent);
    view->show = nemowidget_get_show(parent);
    view->w = width;
    view->h = height;

    NemoWidget *widget;
    view->widget = widget = nemowidget_create_vector(parent, width, height);
    nemowidget_set_alpha(widget, 0, 0, 0, 0.0);

    struct showone *group;
    struct showone *one;
    view->group = group = GROUP_CREATE(nemowidget_get_canvas(widget));

    view->bg = one = RECT_CREATE(group, width, height);
    nemoshow_item_set_fill_color(one, RGBA(WHITE));

    int cnt = 4;
    int i;
    for (i = 1 ; i <= cnt ; i++) {
        char uri[PATH_MAX];
        snprintf(uri, PATH_MAX, "%s/viewer/%d.png", KARIM_IMG_DIR, i);

        ViewerItem *item;
        const char *url = NULL;
        if (i == 2) url = KARIM_LINK_DIR"/2";
        item = viewer_view_create_item(view, parent, uri, url, (double)view->w/cnt);
        view->items = list_append(view->items, item);
    }

    view->title_widget = widget = nemowidget_create_vector(parent, width, height);
    view->title_group = group = GROUP_CREATE(nemowidget_get_canvas(widget));
    nemoshow_item_set_alpha(group, 0.0);

    double sx, sy;
    sx = view->w/1920.0;
    sy = view->h/1080.0;

    double x, y;
    double ww, hh;
    const char *uri;
    x = 0;
    y = 0;
    uri = KARIM_ICON_DIR"/viewer/pink background.svg";
    svg_get_wh(uri, &ww, &hh);
    // Designed for 1920x1080
    ww = ww * sx;
    hh = hh * sy;
    x = x * sx;
    y = y * sy;
    view->title_bg = one = SVG_PATH_GROUP_CREATE(group, ww, hh, uri);
    nemoshow_item_set_alpha(one, 0.5);
    nemoshow_item_set_anchor(one, 0.5, 0.5);
    nemoshow_item_translate(one, x + ww/2, y + hh/2);

    x = 70;
    y = 70;
    uri = KARIM_ICON_DIR"/viewer/title.svg";
    svg_get_wh(uri, &ww, &hh);
    // Designed for 1920x1080
    ww = ww * sx;
    hh = hh * sy;
    x = x * sx;
    y = y * sy;
    view->title0 = one = SVG_PATH_GROUP_CREATE(group, ww, hh, uri);
    nemoshow_item_set_anchor(one, 0.5, 0.5);
    nemoshow_item_scale(one, 0.0, 0.8);
    nemoshow_item_translate(one, x + ww/2, y + hh/2);

    x = 100;
    y = 370;
    uri = KARIM_ICON_DIR"/viewer/subtitle.svg";
    svg_get_wh(uri, &ww, &hh);
    // Designed for 1920x1080
    ww = ww * sx;
    hh = hh * sy;
    x = x * sx;
    y = y * sy;
    view->title1 = one = SVG_PATH_GROUP_CREATE(group, ww, hh, uri);
    nemoshow_item_set_anchor(one, 0.5, 0.5);
    nemoshow_item_scale(one, 0.0, 0.8);
    nemoshow_item_translate(one, x + ww/2, y + hh/2);

    view->title_timer = TOOL_ADD_TIMER(view->tool, 0, _viewer_view_title_timeout, view);

    return view;
}

static void viewer_view_show(ViewerView *view, uint32_t easetype, int duration, int delay)
{
    nemowidget_show(view->widget, 0, 0, 0);
    nemowidget_set_alpha(view->widget, 0, 0, 0, 1.0);

    int cnt = list_count(view->items);
    double w = view->w/cnt ;
    double start = -(cnt - 1) * (w/2);
    int i = 0;

    List *l;
    ViewerItem *item;
    LIST_FOR_EACH(view->items, l, item) {
        viewer_item_mode(item, easetype, duration, delay, VIEWER_MODE_INTRO);
        viewer_item_translate(item, easetype, duration, delay, start + w * i, 0);
        i++;
    }

    nemotimer_set_timeout(view->title_timer, 5000);
    _nemoshow_item_motion(view->title_group, easetype, duration, delay + 500,
            "tx", 50.0, "ty", 50.0,
            "alpha", 1.0, NULL);
    _nemoshow_item_motion(view->title0, easetype, duration, delay + 500,
            "sx", 1.0, "sy", 1.0, NULL);
    _nemoshow_item_motion(view->title1, easetype, duration, delay + 500 + 500,
            "sx", 1.0, "sy", 1.0, NULL);
    nemoshow_dispatch_frame(view->show);
}

static void viewer_view_hide(ViewerView *view, uint32_t easetype, int duration, int delay)
{
    nemowidget_hide(view->widget, 0, 0, 0);
    nemowidget_set_alpha(view->widget, 0, 0, 0, 0.0);

    List *l;
    ViewerItem *item;
    LIST_FOR_EACH(view->items, l, item) {
        viewer_item_mode(item, easetype, duration, delay, VIEWER_MODE_INTRO);
        viewer_item_translate(item, easetype, duration, delay, 0, 0);
    }

    nemotimer_set_timeout(view->title_timer, 0);
    _nemoshow_item_motion(view->title_group, easetype, duration, delay + 500,
            "tx", 0.0, "ty", 0.0,
            "alpha", 0.0, NULL);
    _nemoshow_item_motion(view->title0, easetype, duration, delay + 500,
            "sx", 0.0, "sy", 0.8, NULL);
    _nemoshow_item_motion(view->title1, easetype, duration, delay + 500 + 500,
            "sx", 0.0, "sy", 0.8, NULL);
    nemoshow_dispatch_frame(view->show);
}

struct _HoneyView {
    Karim *karim;
    int w, h;
    struct nemotool *tool;
    struct nemoshow *show;
    NemoWidget *bg_widget;
    Image *bg0;

    NemoWidget *widget;

    int widget_w, widget_h;
    double widget_x, widget_y;
    NemoWidgetGrab *icon_grab;
    NemoWidgetGrab *grab;
    int bg_ix, bg_iy;
    Image *bg;

    struct showone *icon;
};

static void honey_view_show(HoneyView *view, uint32_t easetype, int duration, int delay)
{
    nemowidget_show(view->bg_widget, 0, 0, 0);
    nemowidget_show(view->widget, 0, 0, 0);
    nemowidget_set_alpha(view->bg_widget, easetype, duration + 1000, delay + 1000, 1.0);
    nemowidget_set_alpha(view->widget, easetype, duration, delay, 1.0);

    nemoshow_dispatch_frame(view->show);
}

static void honey_view_hide(HoneyView *view, uint32_t easetype, int duration, int delay)
{
    nemowidget_hide(view->bg_widget, 0, 0, 0);
    nemowidget_hide(view->widget, 0, 0, 0);
    nemowidget_set_alpha(view->bg_widget, easetype, duration, delay, 0.0);
    nemowidget_set_alpha(view->widget, easetype, duration, delay, 0.0);

    nemoshow_dispatch_frame(view->show);
}

static void _honey_view_icon_grab_event(NemoWidgetGrab *grab, NemoWidget *widget, struct showevent *event, void *userdata)
{
    struct nemoshow *show = nemowidget_get_show(widget);
    double ex, ey;
    nemowidget_transform_from_global(widget,
            nemoshow_event_get_x(event),
            nemoshow_event_get_y(event), &ex, &ey);

    struct showone *one = userdata;
    const char *id = nemoshow_one_get_id(one);
    HoneyView *view = nemoshow_one_get_userdata(one);
    RET_IF(!id);

    if (nemoshow_event_is_down(show, event)) {
        _nemoshow_item_motion(view->icon, NEMOEASE_CUBIC_INOUT_TYPE, 500, 0,
                "alpha", 1.0, NULL);
        nemoshow_dispatch_frame(view->show);
    } else if (nemoshow_event_is_up(show, event)) {
        view->icon_grab = NULL;
        if (nemoshow_event_is_single_click(show, event)) {
            Karim *karim = view->karim;
            karim->type = KARIM_TYPE_VIEWER;
            viewer_view_show(karim->viewer, NEMOEASE_CUBIC_OUT_TYPE, 1500, 1000);
            honey_view_hide(karim->honey, NEMOEASE_CUBIC_INOUT_TYPE, 1500, 0);

            // Zooming effect
            double scale = 2.0;
            double sx, sy;
            double ix, iy, iw, ih;
            sx = view->w/3840.0;
            sy = view->h/2160.0;
            ix = 2870.0 * sx * scale;
            iy = 1582.0 * sy * scale;
            iw = 486.0 * sx * scale;
            ih = 420.0 * sy * scale;

            nemowidget_translate(view->widget, NEMOEASE_CUBIC_IN_TYPE, 1000, 0,
                    -(ix - (view->w - iw)/2.0), -(iy - (view->h - ih)/2.0));
            nemowidget_scale(view->widget, NEMOEASE_CUBIC_IN_TYPE, 1000, 0,
                    scale, scale);
            nemoshow_dispatch_frame(view->show);
        } else {
            _nemoshow_item_motion(view->icon, NEMOEASE_CUBIC_INOUT_TYPE, 500, 0,
                    "alpha", 0.0, NULL);
            nemoshow_dispatch_frame(view->show);
        }
    }
}

static void _honey_view_grab_event(NemoWidgetGrab *grab, NemoWidget *widget, struct showevent *event, void *userdata)
{
    struct nemoshow *show = nemowidget_get_show(widget);
    HoneyView *view = userdata;

    if (nemoshow_event_is_motion(show, event)) {
        double ex, ey;
        nemowidget_transform_from_global(widget,
                nemoshow_event_get_x(event),
                nemoshow_event_get_y(event), &ex, &ey);
        double gx, gy;
        nemowidget_transform_from_global(widget,
                nemoshow_event_get_grab_x(event),
                nemoshow_event_get_grab_y(event), &gx, &gy);
        double tx, ty;

        tx = view->widget_x + ex - gx;
        ty = view->widget_y + ey - gy;

        double tw, th;
        tw = view->widget_w - view->w;
        th = view->widget_h - view->h;
        if (tx > 0) {
            tx = 0;
        } else if (tx < -tw) {
            tx = -tw;
        }
        if (ty > 0) {
            ty = 0;
        } else if (ty < -th) {
            ty = -th;
        }
        nemowidget_translate(view->widget, 0, 0, 0, tx, ty);
        nemoshow_dispatch_frame(view->show);
    } else if (nemoshow_event_is_up(show, event)) {
        view->grab = NULL;
        nemowidget_get_geometry(view->widget, &view->widget_x, &view->widget_y, NULL, NULL);
        double tx, ty;
        tx = view->widget_x;
        ty = view->widget_y;
        double tw, th, gap_x, gap_y;
        gap_x = 350;
        gap_y = 350;
        tw = view->widget_w - view->w - gap_x;
        th = view->widget_h - view->h - gap_y;
        if (tx > -gap_x) {
            tx = -gap_x;
        } else if (tx < -tw) {
            tx = -tw;
        }
        if (ty > -gap_y) {
            ty = -gap_y;
        } else if (ty < -th) {
            ty = -th;
        }
        nemowidget_translate(view->widget, NEMOEASE_CUBIC_INOUT_TYPE, 500, 0, tx, ty);
        view->widget_x = tx;
        view->widget_y = ty;
    }
}

static void _honey_view_event(NemoWidget *widget, const char *id, void *info, void *userdata)
{
    struct showevent *event = info;
    HoneyView *view = userdata;
    struct nemoshow *show = nemowidget_get_show(widget);

    if (nemoshow_event_is_down(show, event)) {
        double ex, ey;
        nemowidget_transform_from_global(widget,
                nemoshow_event_get_x(event),
                nemoshow_event_get_y(event), &ex, &ey);

        struct showone *one;
        one = nemowidget_pick_one(view->widget, ex, ey);
        if (one && !view->icon_grab) {
            view->icon_grab = nemowidget_create_grab(widget, event,
                    _honey_view_icon_grab_event, one);
        } else {
            view->grab = nemowidget_create_grab(widget, event,
                    _honey_view_grab_event, view);
        }
    }
}

static HoneyView *honey_view_create(Karim *karim, NemoWidget *parent, int width, int height)
{
    HoneyView *view = calloc(sizeof(HoneyView), 1);
    view->karim = karim;
    view->tool = nemowidget_get_tool(parent);
    view->show = nemowidget_get_show(parent);
    view->w = width;
    view->h = height;

    double sx, sy;
    sx = view->w/3840.0;
    sy = view->h/2160.0;

    NemoWidget *widget;
    view->bg_widget = widget = nemowidget_create_vector(parent, view->w, view->h);
    nemowidget_set_alpha(widget, 0, 0, 0, 0.0);

    const char *uri;
    uri = KARIM_IMG_DIR"/honey/background0.png";

    Image *img;
    view->bg0 = img = image_create(nemowidget_get_canvas(widget));
    image_load_full(img, view->tool, uri, view->w, view->h, NULL, NULL);

    int w, h;
    uri = KARIM_IMG_DIR"/honey/background.png";
    file_get_image_wh(uri, &w, &h);

    view->widget_w = w = w * sx;
    view->widget_h = h = h * sy;
    view->bg_ix = (w - view->w)/2;
    view->bg_iy = (h - view->h)/2;

    view->widget = widget = nemowidget_create_vector(parent, w, h);
    nemowidget_set_alpha(widget, 0, 0, 0, 0.0);
    nemowidget_translate(widget, 0, 0, 0, -view->bg_ix, -view->bg_iy);
    nemowidget_append_callback(widget, "event", _honey_view_event, view);
    view->widget_x = -view->bg_ix;
    view->widget_y = -view->bg_iy;

    struct showone *canvas;
    struct showone *one;
    canvas = nemowidget_get_canvas(widget);

    view->bg = img = image_create(canvas);
    image_load_full(img, view->tool, uri, w, h, NULL, NULL);
    //image_translate(img, 0, 0, 0, -view->bg_ix, -view->bg_iy);

    double x, y;
    double ww, hh;
    uri = KARIM_ICON_DIR"/honey/icon-selected.svg";
    svg_get_wh(uri, &ww, &hh);
    ww = ww * sx;
    hh = hh * sy;
    x = 2802 * sx;
    y = 1613 * sy;
    view->icon = one = SVG_PATH_GROUP_CREATE(canvas, ww, hh, uri);
    nemoshow_item_translate(one, x, y);
    nemoshow_item_set_alpha(one, 0.0);
    nemoshow_one_set_state(one, NEMOSHOW_PICK_STATE);
    nemoshow_one_set_id(one, "icon");
    nemoshow_one_set_userdata(one, view);

    return view;
}

struct _YearView {
    Karim *karim;
    int w, h;
    struct nemotool *tool;
    struct nemoshow *show;

    NemoWidget *bg_widget;
    struct showone *bg_group;
    int bg_w;
    int bg_cnt_x;
    int bg_idx;
    struct nemotimer *bg_timer;
    struct showone *bg;
    List *bgs;

    NemoWidget *widget;
    int btn_h;
    NemoWidgetGrab *btn_grab;
    struct nemotimer *btn_timer;

    struct showone *btn_group;
    struct showone *btn_bg;

    struct showone *btn_9498_bg;
    struct showone *btn_9903_bg;
    struct showone *btn_0408_bg;
    struct showone *btn_0913_bg;
    struct showone *btn_1416_bg;
    struct showone *btn_9498;
    struct showone *btn_9903;
    struct showone *btn_0408;
    struct showone *btn_0408_sel;
    struct showone *btn_0913;
    struct showone *btn_1416;

    struct showone *btn_subline;
    struct showone *btn_2004_bg;
    struct showone *btn_2005_bg;
    struct showone *btn_2006_bg;
    struct showone *btn_2007_bg;
    struct showone *btn_2008_bg;
    struct showone *btn_2004;
    struct showone *btn_2005;
    struct showone *btn_2006;
    struct showone *btn_2007;
    struct showone *btn_2008;
};

typedef struct _YearBG YearBG;
struct _YearBG {
    YearView *view;
    NemoWidget *widget;
    struct showone *group;
    List *imgs;
};

YearBG *year_bg_create(YearView *view, NemoWidget *parent, const char *uri, int w, int h, int cnt_y)
{
    YearBG *bg = calloc(sizeof(YearBG), 1);
    bg->view = view;

    NemoWidget *widget;
    struct showone *group;

    bg->widget = widget = nemowidget_create_vector(parent, w, h * cnt_y);
    nemowidget_set_alpha(widget, 0, 0, 0, 0.0);
    bg->group = group = GROUP_CREATE(nemowidget_get_canvas(widget));

    int j;
    for (j = 0 ; j < cnt_y ; j++) {
        Image *img = image_create(group);
        image_translate(img, 0, 0, 0, 0, h * j);
        image_load_full(img, view->tool, uri, w, h, NULL, NULL);
        bg->imgs = list_append(bg->imgs, img);
    }
    return bg;
}

void year_bg_translate(YearBG *bg, uint32_t easetype, int duration, int delay, double tx, double ty)
{
    nemowidget_translate(bg->widget, easetype, duration, delay, tx, ty);
}

static void _year_view_btn_timeout(struct nemotimer *timer, void *userdata)
{
    int duration = 2000;
    int delay = 0;
    YearView *view = userdata;

    NemoMotion *m;
    m = nemomotion_create(view->show, NEMOEASE_LINEAR_TYPE, duration, delay);
    nemomotion_attach(m, 0.5,
            view->btn_9498_bg, "sx", 1.25,
            view->btn_9498_bg, "sy", 1.25,
            NULL);
    nemomotion_attach(m, 1.0,
            view->btn_9498_bg, "sx", 1.0,
            view->btn_9498_bg, "sy", 1.0,
            NULL);
    nemomotion_run(m);
    delay += 500;
    m = nemomotion_create(view->show, NEMOEASE_LINEAR_TYPE, duration, delay);
    nemomotion_attach(m, 0.5,
            view->btn_9903_bg, "sx", 1.20,
            view->btn_9903_bg, "sy", 1.20,
            NULL);
    nemomotion_attach(m, 1.0,
            view->btn_9903_bg, "sx", 1.0,
            view->btn_9903_bg, "sy", 1.0,
            NULL);
    nemomotion_run(m);
    delay += 500;
    m = nemomotion_create(view->show, NEMOEASE_LINEAR_TYPE, duration, delay);
    nemomotion_attach(m, 0.5,
            view->btn_0408_bg, "sx", 1.25,
            view->btn_0408_bg, "sy", 1.25,
            NULL);
    nemomotion_attach(m, 1.0,
            view->btn_0408_bg, "sx", 1.0,
            view->btn_0408_bg, "sy", 1.0,
            NULL);
    nemomotion_run(m);
    delay += 500;
    m = nemomotion_create(view->show, NEMOEASE_LINEAR_TYPE, duration, delay);
    nemomotion_attach(m, 0.5,
            view->btn_0913_bg, "sx", 1.25,
            view->btn_0913_bg, "sy", 1.25,
            NULL);
    nemomotion_attach(m, 1.0,
            view->btn_0913_bg, "sx", 1.0,
            view->btn_0913_bg, "sy", 1.0,
            NULL);
    nemomotion_run(m);
    delay += 500;
    m = nemomotion_create(view->show, NEMOEASE_LINEAR_TYPE, duration, delay);
    nemomotion_attach(m, 0.5,
            view->btn_1416_bg, "sx", 1.25,
            view->btn_1416_bg, "sy", 1.25,
            NULL);
    nemomotion_attach(m, 1.0,
            view->btn_1416_bg, "sx", 1.0,
            view->btn_1416_bg, "sy", 1.0,
            NULL);
    nemomotion_run(m);
    nemotimer_set_timeout(timer, duration + delay);
}

static void _year_view_bg_timeout(struct nemotimer *timer, void *userdata)
{
    int duration = 15000;
    YearView *view = userdata;
    int i = 0;
    List *l;
    l = list_get_nth(view->bgs, view->bg_idx);
    while (l) {
        YearBG *bg = LIST_DATA(l);
        year_bg_translate(bg, 0, 0, 0, i * view->bg_w, 0);
        year_bg_translate(bg, NEMOEASE_LINEAR_TYPE, duration, 0, (i - 1) * view->bg_w, 0);

        l = l->next;
        if (!l) l = LIST_FIRST(view->bgs);
        i++;
        if (i >= view->bg_cnt_x) break;
    }

    nemotimer_set_timeout(timer, duration);
}

static void _year_view_grab_event(NemoWidgetGrab *grab, NemoWidget *widget, struct showevent *event, void *userdata)
{
    struct nemoshow *show = nemowidget_get_show(widget);
    double ex, ey;
    nemowidget_transform_from_global(widget,
            nemoshow_event_get_x(event),
            nemoshow_event_get_y(event), &ex, &ey);

    struct showone *one = userdata;
    uint32_t tag = nemoshow_one_get_tag(one);
    YearView *view = nemoshow_one_get_userdata(one);

    if (nemoshow_event_is_down(show, event)) {
        if (tag == 0x0408) {
            _nemoshow_item_motion_bounce(view->btn_0408, NEMOEASE_CUBIC_INOUT_TYPE, 500, 0,
                    "alpha", 0.0, 0.0,
                    NULL);
            _nemoshow_item_motion_bounce(view->btn_0408_sel, NEMOEASE_CUBIC_INOUT_TYPE, 500, 0,
                    "sx", 1.3, 1.2, "sy", 1.3, 1.2,
                    "alpha", 1.0, 1.0,
                    NULL);
            nemoshow_dispatch_frame(view->show);
        }
    } else if (nemoshow_event_is_up(show, event)) {
        view->btn_grab = NULL;
        if (tag == 0x0408) {
            if (nemoshow_event_is_single_click(show, event)) {
                uint32_t easetype = NEMOEASE_CUBIC_INOUT_TYPE;
                int duration = 1000;
                int delay = 0;
                _nemoshow_item_motion_bounce(view->btn_subline, easetype, duration, 0,
                        "sx", 1.2, 1.0, NULL);
                _nemoshow_item_motion_bounce(view->btn_2004_bg, easetype, duration, delay + 250,
                        "sx", 1.2, 1.0, "sy", 1.2, 1.0, NULL);
                _nemoshow_item_motion_bounce(view->btn_2005_bg, easetype, duration, delay + 250 * 2,
                        "sx", 1.2, 1.0, "sy", 1.2, 1.0, NULL);
                _nemoshow_item_motion_bounce(view->btn_2006_bg, easetype, duration, delay + 250 * 3,
                        "sx", 1.2, 1.0, "sy", 1.2, 1.0, NULL);
                _nemoshow_item_motion_bounce(view->btn_2007_bg, easetype, duration, delay + 250 * 4,
                        "sx", 1.2, 1.0, "sy", 1.2, 1.0, NULL);
                _nemoshow_item_motion_bounce(view->btn_2008_bg, easetype, duration, delay + 250 * 5,
                        "sx", 1.2, 1.0, "sy", 1.2, 1.0, NULL);
                _nemoshow_item_motion_bounce(view->btn_2004, easetype, duration, delay + 250 * 2,
                        "sx", 1.2, 1.0, "sy", 1.2, 1.0,
                        "ro", -160.0, -140.0,
                        NULL);
                _nemoshow_item_motion_bounce(view->btn_2005, easetype, duration, delay + 250 * 3,
                        "sx", 1.2, 1.0, "sy", 1.2, 1.0,
                        "ro", -150.0, -130.0,
                        NULL);
                _nemoshow_item_motion_bounce(view->btn_2006, easetype, duration, delay + 250 * 4,
                        "sx", 1.2, 1.0, "sy", 1.2, 1.0,
                        "ro", -120.0, -100.0,
                        NULL);
                _nemoshow_item_motion_bounce(view->btn_2007, easetype, duration, delay + 250 * 5,
                        "sx", 1.2, 1.0, "sy", 1.2, 1.0,
                        "ro", -60.0, -80.0,
                        NULL);
                _nemoshow_item_motion_bounce(view->btn_2008, easetype, duration, delay + 250 * 6,
                        "sx", 1.2, 1.0, "sy", 1.2, 1.0,
                        "ro", -40.0, -60.0,
                        NULL);

                _nemoshow_item_motion_bounce(view->btn_0408_sel, NEMOEASE_CUBIC_INOUT_TYPE, 500, 0,
                        "sx", 0.8, 1.0, "sy", 0.8, 1.0,
                        NULL);
                nemoshow_dispatch_frame(view->show);
            } else {
                _nemoshow_item_motion_bounce(view->btn_0408_sel, NEMOEASE_CUBIC_INOUT_TYPE, 500, 0,
                        "alpha", 0.0, 0.0,
                        NULL);
                _nemoshow_item_motion_bounce(view->btn_0408, NEMOEASE_CUBIC_INOUT_TYPE, 500, 0,
                        "sx", 0.8, 1.0, "sy", 0.8, 1.0,
                        "alpha", 1.0, 1.0,
                        NULL);
                nemoshow_dispatch_frame(view->show);
            }
        }
    }
}

static void _year_view_event(NemoWidget *widget, const char *id, void *info, void *userdata)
{
    struct showevent *event = info;
    YearView *view = userdata;
    struct nemoshow *show = nemowidget_get_show(widget);

    double ex, ey;
    nemowidget_transform_from_global(widget,
            nemoshow_event_get_x(event),
            nemoshow_event_get_y(event), &ex, &ey);

    if (nemoshow_event_is_down(show, event)) {
        struct showone *one;
        one = nemowidget_pick_one(view->widget, ex, ey);
        if (one && !view->btn_grab) {
            view->btn_grab = nemowidget_create_grab(widget, event,
                    _year_view_grab_event, one);
        } else {
            /*
            IntroEffect *fx = malloc(sizeof(IntroEffect));
            fx->time = time(NULL);
            view->touch_effects = list_append(view->touch_effects, fx);
            fx->ones = NULL;

            one = path_draw(view->path0, view->group);
            nemoshow_item_set_anchor(one, 0.5, 0.5);
            nemoshow_item_translate(one, ex, ey);
            nemoshow_item_scale(one, 0.0, 0.0);
            nemoshow_item_set_alpha(one, 1.0);
            fx->ones = list_append(fx->ones, one);

            List *l;
            Path *path;
            LIST_FOR_EACH(view->paths, l, path) {
                one = path_draw(path, view->group);
                nemoshow_item_set_anchor(one, 0.5, 0.5);
                nemoshow_item_translate(one, ex, ey);
                nemoshow_item_scale(one, 0.0, 0.0);
                nemoshow_item_set_alpha(one, 1.0);
                fx->ones = list_append(fx->ones, one);
            }

            one = path_draw(view->path1, view->group);
            nemoshow_item_set_anchor(one, 0.5, 0.5);
            nemoshow_item_translate(one, ex, ey);
            nemoshow_item_scale(one, 0.0, 0.0);
            nemoshow_item_set_alpha(one, 1.0);
            fx->ones = list_append(fx->ones, one);

            int delay = 0;
            LIST_FOR_EACH_REVERSE(fx->ones, l, one) {
                _nemoshow_item_motion(one, NEMOEASE_CUBIC_OUT_TYPE, 5000, delay,
                        "sx", 1.0, "sy", 1.0,
                        "alpha", 0.0,
                        "ro", 120.0,
                        NULL);
                delay += 20;
            }
            */
        }
    }
}

static YearView *year_view_create(Karim *karim, NemoWidget *parent, int width, int height)
{
    YearView *view = calloc(sizeof(YearView), 1);
    view->karim = karim;
    view->tool = nemowidget_get_tool(parent);
    view->show = nemowidget_get_show(parent);
    view->w = width;
    view->h = height;

    struct showone *group;
    struct showone *one;

    // Background
    NemoWidget *widget;
    view->bg_widget = widget = nemowidget_create_vector(parent, width, height);
    nemowidget_set_alpha(widget, 0, 0, 0, 0.0);
    view->bg_group = group = GROUP_CREATE(nemowidget_get_canvas(widget));

    view->bg = RECT_CREATE(group, width, height);
    nemoshow_item_set_fill_color(view->bg, RGBA(WHITE));

    int w, h;
    const char *uri = KARIM_IMG_DIR"/year/year-background.png";
    file_get_image_wh(uri, &w, &h);
    int cnt_x, cnt_y;
    view->bg_w = w;
    view->bg_cnt_x = cnt_x = width/w + 2;
    cnt_y = height/h + 1;

    int i;
    for (i = 0 ; i < cnt_x ; i++) {
        YearBG *bg = calloc(sizeof(YearBG), 1);
        bg = year_bg_create(view, parent, uri, w, h, cnt_y);
        year_bg_translate(bg, 0, 0, 0, i * w, 0);
        view->bgs = list_append(view->bgs, bg);
    }
    view->bg_timer = TOOL_ADD_TIMER(view->tool, 0, _year_view_bg_timeout, view);

    view->btn_timer = TOOL_ADD_TIMER(view->tool, 0, _year_view_btn_timeout, view);

    view->widget = widget = nemowidget_create_vector(parent, width, height);
    nemowidget_set_alpha(widget, 0, 0, 0, 0.0);
    nemowidget_append_callback(widget, "event", _year_view_event, view);

    view->btn_group = group = GROUP_CREATE(nemowidget_get_canvas(widget));
    nemoshow_item_translate(group, width/2, -height);

    double sx ,sy;
    double x, y;
    double ww, hh;
    double pw, ph;
    sx = view->w/1920.0;
    sy = view->h/1080.0;
    uri = KARIM_ICON_DIR"/year/year-pink.svg";
    svg_get_wh(uri, &ww, &hh);
    ww = ww * sx;
    hh = hh * sy;
    view->btn_bg = one = SVG_PATH_GROUP_CREATE(group, ww, hh, uri);
    nemoshow_item_set_anchor(one, 0.5, 0.5);
    pw = ww;
    view->btn_h = ph = hh;

    x = 11.443, y = 35.751;
    uri = KARIM_ICON_DIR"/year/year-button-1994_1998.svg";
    svg_get_wh(uri, &ww, &hh);
    // Designed for 1920x1080
    ww = ww * sx;
    hh = hh * sy;
    x = x * sx;
    y = y * sy;
    view->btn_9498_bg = one = CIRCLE_CREATE(group, ww/2);
    nemoshow_item_set_alpha(one, 0.0);
    nemoshow_item_set_fill_color(one, RGBA(0xe11A8EFF));
    nemoshow_item_translate(one, x - (pw - ww)/2, y - (ph - hh)/2);
    view->btn_9498 = one = SVG_PATH_GROUP_CREATE(group, ww, hh, uri);
    nemoshow_item_translate(one, x - (pw - ww)/2, y - (ph - hh)/2);
    nemoshow_item_set_anchor(one, 0.5, 0.5);
    nemoshow_item_scale(one, 0.0, 0.0);

    x = 195.548, y = 247.52;
    uri = KARIM_ICON_DIR"/year/year-button-1999_2003.svg";
    svg_get_wh(uri, &ww, &hh);
    // Designed for 1920x1080
    ww = ww * sx;
    hh = hh * sy;
    x = x * sx;
    y = y * sy;
    view->btn_9903_bg = one = CIRCLE_CREATE(group, ww/2);
    nemoshow_item_set_alpha(one, 0.0);
    nemoshow_item_set_fill_color(one, RGBA(0xe11A8EFF));
    nemoshow_item_translate(one, x - (pw - ww)/2, y - (ph - hh)/2);
    view->btn_9903 = one = SVG_PATH_GROUP_CREATE(group, ww, hh, uri);
    nemoshow_item_translate(one, x - (pw - ww)/2, y - (ph - hh)/2);
    nemoshow_item_set_anchor(one, 0.5, 0.5);
    nemoshow_item_scale(one, 0.0, 0.0);

    x = 351.828, y = 13.397;
    uri = KARIM_ICON_DIR"/year/year-button-2004_2008.svg";
    svg_get_wh(uri, &ww, &hh);
    // Designed for 1920x1080
    ww = ww * sx;
    hh = hh * sy;
    x = x * sx;
    y = y * sy;
    view->btn_0408_bg = one = CIRCLE_CREATE(group, ww/2);
    nemoshow_item_set_alpha(one, 0.0);
    nemoshow_item_set_fill_color(one, RGBA(0xe11A8EFF));
    nemoshow_item_translate(one, x - (pw - ww)/2, y - (ph - hh)/2);
    view->btn_0408 = one = SVG_PATH_GROUP_CREATE(group, ww, hh, uri);
    nemoshow_item_translate(one, x - (pw - ww)/2, y - (ph - hh)/2);
    nemoshow_one_set_state(one, NEMOSHOW_PICK_STATE);
    nemoshow_one_set_tag(one, 0x0408);
    nemoshow_one_set_userdata(one, view);
    nemoshow_item_set_anchor(one, 0.5, 0.5);
    nemoshow_item_scale(one, 0.0, 0.0);

    uri = KARIM_ICON_DIR"/year/year-button-2004_2008-selected.svg";
    svg_get_wh(uri, &ww, &hh);
    // Designed for 1920x1080
    ww = ww * sx;
    hh = hh * sy;
    view->btn_0408_sel = one = SVG_PATH_GROUP_CREATE(group, ww, hh, uri);
    nemoshow_item_translate(one, x - (pw - ww)/2, y - (ph - hh)/2);
    nemoshow_item_set_anchor(one, 0.5, 0.5);
    nemoshow_item_set_alpha(one, 0.0);

    x = 550.017, y = 329.35;
    uri = KARIM_ICON_DIR"/year/year-button-2009_2013.svg";
    svg_get_wh(uri, &ww, &hh);
    // Designed for 1920x1080
    ww = ww * sx;
    hh = hh * sy;
    x = x * sx;
    y = y * sy;
    view->btn_0913_bg = one = CIRCLE_CREATE(group, ww/2);
    nemoshow_item_set_alpha(one, 0.0);
    nemoshow_item_set_fill_color(one, RGBA(0xe11A8EFF));
    nemoshow_item_translate(one, x - (pw - ww)/2, y - (ph - hh)/2);
    view->btn_0913 = one = SVG_PATH_GROUP_CREATE(group, ww, hh, uri);
    nemoshow_item_translate(one, x - (pw - ww)/2, y - (ph - hh)/2);
    nemoshow_item_set_anchor(one, 0.5, 0.5);
    nemoshow_item_scale(one, 0.0, 0.0);

    x = 752.957, y = 21.982;
    uri = KARIM_ICON_DIR"/year/year-button-2014_2016.svg";
    svg_get_wh(uri, &ww, &hh);
    // Designed for 1920x1080
    ww = ww * sx;
    hh = hh * sy;
    x = x * sx;
    y = y * sy;
    view->btn_1416_bg = one = CIRCLE_CREATE(group, ww/2);
    nemoshow_item_set_alpha(one, 0.0);
    nemoshow_item_set_fill_color(one, RGBA(0xe11A8EFF));
    nemoshow_item_translate(one, x - (pw - ww)/2, y - (ph - hh)/2);
    view->btn_1416 = one = SVG_PATH_GROUP_CREATE(group, ww, hh, uri);
    nemoshow_item_translate(one, x - (pw - ww)/2, y - (ph - hh)/2);
    nemoshow_item_set_anchor(one, 0.5, 0.5);
    nemoshow_item_scale(one, 0.0, 0.0);

    x = 287, y = -55;
    uri = KARIM_ICON_DIR"/year/year-subline.svg";
    svg_get_wh(uri, &ww, &hh);
    // Designed for 1920x1080
    ww = ww * sx;
    hh = hh * sy;
    x = x * sx;
    y = y * sy;
    view->btn_subline = one = SVG_PATH_GROUP_CREATE(group, ww, hh, uri);
    nemoshow_item_translate(one, x - (pw - ww)/2, y - (ph - hh)/2);
    nemoshow_item_set_anchor(one, 0.5, 0.5);
    nemoshow_item_scale(one, 0.0, 1.0);

    // 2004
    x = 263.129, y = 26.468;
    uri = KARIM_ICON_DIR"/year/year-subline-subcircle.svg";
    svg_get_wh(uri, &ww, &hh);
    // Designed for 1920x1080
    ww = ww * sx;
    hh = hh * sy;
    x = x * sx;
    y = y * sy;
    view->btn_2004_bg = one = SVG_PATH_GROUP_CREATE(group, ww, hh, uri);
    nemoshow_item_translate(one, x - (pw - ww)/2, y - (ph - hh)/2);
    nemoshow_item_set_anchor(one, 0.5, 0.5);
    nemoshow_item_scale(one, 0.0, 0.0);

    uri = KARIM_ICON_DIR"/year/2004.svg";
    svg_get_wh(uri, &ww, &hh);
    // Designed for 1920x1080
    ww = ww * sx;
    hh = hh * sy;
    view->btn_2004 = one = SVG_PATH_GROUP_CREATE(group, ww, hh, uri);
    nemoshow_item_translate(one, x - (pw - ww)/2 - 80 * sx, y - (ph - hh)/2 - 30 * sy);
    nemoshow_item_set_anchor(one, 0.5, 0.5);
    nemoshow_item_scale(one, 0.0, 0.0);
    nemoshow_item_rotate(one, 0);

    // 2005
    x = 309.026, y = -43.416;
    uri = KARIM_ICON_DIR"/year/year-subline-subcircle.svg";
    svg_get_wh(uri, &ww, &hh);
    // Designed for 1920x1080
    ww = ww * sx;
    hh = hh * sy;
    x = x * sx;
    y = y * sy;
    view->btn_2005_bg = one = SVG_PATH_GROUP_CREATE(group, ww, hh, uri);
    nemoshow_item_translate(one, x - (pw - ww)/2, y - (ph - hh)/2);
    nemoshow_item_set_anchor(one, 0.5, 0.5);
    nemoshow_item_scale(one, 0.0, 0.0);

    uri = KARIM_ICON_DIR"/year/2005.svg";
    svg_get_wh(uri, &ww, &hh);
    // Designed for 1920x1080
    ww = ww * sx;
    hh = hh * sy;
    view->btn_2005 = one = SVG_PATH_GROUP_CREATE(group, ww, hh, uri);
    nemoshow_item_translate(one, x - (pw - ww)/2 - 65 * sx, y - (ph - hh)/2 - 45 * sy);
    nemoshow_item_set_anchor(one, 0.5, 0.5);
    nemoshow_item_scale(one, 0.0, 0.0);
    nemoshow_item_rotate(one, 0);

    // 2006
    x = 383.705, y = -81.006;
    uri = KARIM_ICON_DIR"/year/year-subline-subcircle.svg";
    svg_get_wh(uri, &ww, &hh);
    // Designed for 1920x1080
    ww = ww * sx;
    hh = hh * sy;
    x = x * sx;
    y = y * sy;
    view->btn_2006_bg = one = SVG_PATH_GROUP_CREATE(group, ww, hh, uri);
    nemoshow_item_translate(one, x - (pw - ww)/2, y - (ph - hh)/2);
    nemoshow_item_set_anchor(one, 0.5, 0.5);
    nemoshow_item_scale(one, 0.0, 0.0);

    uri = KARIM_ICON_DIR"/year/2006.svg";
    svg_get_wh(uri, &ww, &hh);
    // Designed for 1920x1080
    ww = ww * sx;
    hh = hh * sy;
    view->btn_2006 = one = SVG_PATH_GROUP_CREATE(group, ww, hh, uri);
    nemoshow_item_translate(one, x - (pw - ww)/2 - 30 * sx, y - (ph - hh)/2 - 60 * sy);
    nemoshow_item_set_anchor(one, 0.5, 0.5);
    nemoshow_item_scale(one, 0.0, 0.0);
    nemoshow_item_rotate(one, 00);

    // 2007
    x = 467.174, y = -76.213;
    uri = KARIM_ICON_DIR"/year/year-subline-subcircle.svg";
    svg_get_wh(uri, &ww, &hh);
    // Designed for 1920x1080
    ww = ww * sx;
    hh = hh * sy;
    x = x * sx;
    y = y * sy;
    view->btn_2007_bg = one = SVG_PATH_GROUP_CREATE(group, ww, hh, uri);
    nemoshow_item_translate(one, x - (pw - ww)/2, y - (ph - hh)/2);
    nemoshow_item_set_anchor(one, 0.5, 0.5);
    nemoshow_item_scale(one, 0.0, 0.0);

    uri = KARIM_ICON_DIR"/year/2007.svg";
    svg_get_wh(uri, &ww, &hh);
    // Designed for 1920x1080
    ww = ww * sx;
    hh = hh * sy;
    view->btn_2007 = one = SVG_PATH_GROUP_CREATE(group, ww, hh, uri);
    nemoshow_item_translate(one, x - (pw - ww)/2 - 0, y - (ph - hh)/2 - 60 * sy);
    nemoshow_item_set_anchor(one, 0.5, 0.5);
    nemoshow_item_scale(one, 0.0, 0.0);
    nemoshow_item_rotate(one, -120);

    // 2008
    x = 537.074, y = -30.317;
    uri = KARIM_ICON_DIR"/year/year-subline-subcircle.svg";
    svg_get_wh(uri, &ww, &hh);
    // Designed for 1920x1080
    ww = ww * sx;
    hh = hh * sy;
    x = x * sx;
    y = y * sy;
    view->btn_2008_bg = one = SVG_PATH_GROUP_CREATE(group, ww, hh, uri);
    nemoshow_item_translate(one, x - (pw - ww)/2, y - (ph - hh)/2);
    nemoshow_item_set_anchor(one, 0.5, 0.5);
    nemoshow_item_scale(one, 0.0, 0.0);

    uri = KARIM_ICON_DIR"/year/2008.svg";
    svg_get_wh(uri, &ww, &hh);
    // Designed for 1920x1080
    ww = ww * sx;
    hh = hh * sy;
    view->btn_2008 = one = SVG_PATH_GROUP_CREATE(group, ww, hh, uri);
    nemoshow_item_translate(one, x - (pw - ww)/2 + 30 * sx, y - (ph - hh)/2 - 50 * sy);
    nemoshow_item_set_anchor(one, 0.5, 0.5);
    nemoshow_item_scale(one, 0.0, 0.0);
    nemoshow_item_rotate(one, -120);

    return view;
}

static void year_view_show(YearView *view, uint32_t easetype, int duration, int delay)
{
    nemowidget_show(view->widget, 0, 0, 0);
    nemowidget_show(view->bg_widget, 0, 0, 0);
    nemowidget_set_alpha(view->widget, easetype, duration, delay, 1.0);
    nemowidget_set_alpha(view->bg_widget, easetype, duration, delay, 1.0);
    List *l;
    YearBG *bg;
    LIST_FOR_EACH(view->bgs, l, bg) {
        nemowidget_set_alpha(bg->widget, easetype, duration, delay, 1.0);
    }

    _nemoshow_item_motion(view->btn_group, easetype, duration, delay,
            "ty", view->h/5.0 + view->btn_h/2, NULL);
    _nemoshow_item_motion(view->btn_9498_bg, easetype, duration, delay, "alpha", 1.0, NULL);
    _nemoshow_item_motion(view->btn_9903_bg, easetype, duration, delay, "alpha", 1.0, NULL);
    _nemoshow_item_motion(view->btn_0408_bg, easetype, duration, delay, "alpha", 1.0, NULL);
    _nemoshow_item_motion(view->btn_0913_bg, easetype, duration, delay, "alpha", 1.0, NULL);
    _nemoshow_item_motion(view->btn_1416_bg, easetype, duration, delay, "alpha", 1.0, NULL);

    _nemoshow_item_motion_bounce(view->btn_9498, easetype, duration, delay + 500,
            "sx", 1.2, 1.0, "sy", 1.2, 1.0, NULL);
    _nemoshow_item_motion_bounce(view->btn_9903, easetype, duration, delay + 500 + 250,
            "sx", 1.2, 1.0, "sy", 1.2, 1.0, NULL);
    _nemoshow_item_motion_bounce(view->btn_0408, easetype, duration, delay + 500 + 250 * 2,
            "sx", 1.2, 1.0, "sy", 1.2, 1.0, NULL);
    _nemoshow_item_motion_bounce(view->btn_0913, easetype, duration, delay + 500 + 250 * 3,
            "sx", 1.2, 1.0, "sy", 1.2, 1.0, NULL);
    _nemoshow_item_motion_bounce(view->btn_1416, easetype, duration, delay + 500 + 250 * 4,
            "sx", 1.2, 1.0, "sy", 1.2, 1.0, NULL);

    view->bg_idx = 0;
    nemotimer_set_timeout(view->bg_timer, duration + delay);
    nemotimer_set_timeout(view->btn_timer, duration + delay);
    nemoshow_dispatch_frame(view->show);
}

static void year_view_hide(YearView *view, uint32_t easetype, int duration, int delay)
{
    nemowidget_hide(view->widget, 0, 0, 0);
    nemowidget_hide(view->bg_widget, 0, 0, 0);
    nemowidget_set_alpha(view->widget, easetype, duration, delay, 0.0);
    nemowidget_set_alpha(view->bg_widget, easetype, duration, delay, 0.0);
    int i = 0;
    List *l;
    YearBG *bg;
    LIST_FOR_EACH(view->bgs, l, bg) {
        year_bg_translate(bg, easetype, duration, delay, i * view->bg_w, 0);
        nemowidget_set_alpha(bg->widget, easetype, duration, delay, 0.0);
        i++;
    }

    _nemoshow_item_motion(view->btn_group, easetype, duration, delay + 500,
            "ty", 0.0, NULL);
    // XXX: this should not be shown solely
    /*
    _nemoshow_item_motion(view->btn_9498_bg, easetype, duration, delay, "alpha", 0.0, NULL);
    _nemoshow_item_motion(view->btn_9903_bg, easetype, duration, delay, "alpha", 0.0, NULL);
    _nemoshow_item_motion(view->btn_0408_bg, easetype, duration, delay, "alpha", 0.0, NULL);
    _nemoshow_item_motion(view->btn_0913_bg, easetype, duration, delay, "alpha", 0.0, NULL);
    _nemoshow_item_motion(view->btn_1416_bg, easetype, duration, delay, "alpha", 0.0, NULL);
    */

    _nemoshow_item_motion(view->btn_9498, easetype, duration, delay,
            "sx", 0.0, "sy", 0.0, NULL);
    _nemoshow_item_motion(view->btn_9903, easetype, duration, delay,
            "sx", 0.0, "sy", 0.0, NULL);
    _nemoshow_item_motion(view->btn_0408, easetype, duration, delay,
            "sx", 0.0, "sy", 0.0, NULL);
    _nemoshow_item_motion(view->btn_0913, easetype, duration, delay,
            "sx", 0.0, "sy", 0.0, NULL);
    _nemoshow_item_motion(view->btn_1416, easetype, duration, delay,
            "sx", 0.0, "sy", 0.0, NULL);

    nemotimer_set_timeout(view->bg_timer, 0);
    nemotimer_set_timeout(view->btn_timer, 0);

#if 0
    // XXX: recover select state
    _nemoshow_item_motion(view->btn_subline, easetype, duration, delay,
            "sx", 0.0, NULL);
    _nemoshow_item_motion(view->btn_2004_bg, easetype, duration, delay,
            "sx", 0.0, "sy", 0.0, NULL);
    _nemoshow_item_motion(view->btn_2005_bg, easetype, duration, delay,
            "sx", 0.0, "sy", 0.0, NULL);
    _nemoshow_item_motion(view->btn_2006_bg, easetype, duration, delay,
            "sx", 0.0, "sy", 0.0, NULL);
    _nemoshow_item_motion(view->btn_2007_bg, easetype, duration, delay,
            "sx", 0.0, "sy", 0.0, NULL);
    _nemoshow_item_motion(view->btn_2008_bg, easetype, duration, delay,
            "sx", 0.0, "sy", 0.0, NULL);
    _nemoshow_item_motion(view->btn_2004, easetype, duration, delay,
            "sx", 0.0, "sy", 0.0, "ro", 0.0, NULL);
    _nemoshow_item_motion(view->btn_2005, easetype, duration, delay,
            "sx", 0.0, "sy", 0.0, "ro", 0.0, NULL);
    _nemoshow_item_motion(view->btn_2006, easetype, duration, delay,
            "sx", 0.0, "sy", 0.0, "ro", 0.0, NULL);
    _nemoshow_item_motion(view->btn_2007, easetype, duration, delay,
            "sx", 0.0, "sy", 0.0, "ro", 0.0, NULL);
    _nemoshow_item_motion(view->btn_2008, easetype, duration, delay,
            "sx", 0.0, "sy", 0.0, "ro", 0.0, NULL);
    _nemoshow_item_motion(view->btn_0408, easetype, duration, delay,
            "alpha", 1.0,
            NULL);
    _nemoshow_item_motion(view->btn_0408_sel, easetype, duration, delay,
            "sx", 0.0, "sy", 0.0,
            NULL);
#endif

    nemoshow_dispatch_frame(view->show);
}

typedef struct _Coord Coord;
struct _Coord {
    double x, y;
};

Coord REGION_TXT_COORDS[45] = {
    {301, 250},
    {330, 366},
    {265, 465},
    {393,599},
    {416, 663},
    {591, 662},
    {830, 146},
    {799, 186},
    {767, 215},
    {704, 233},
    {657, 252},
    {707, 271},
    {657, 290},
    {719, 309},
    {692, 328},
    {733, 347},
    {1006, 153},
    {1006, 172},
    {1006, 191},
    {1006, 210},
    {1006, 229},
    {1070, 248},
    {1070, 267},
    {1070, 286},
    {1070, 305},
    {1070, 324},
    {1022, 366},
    {945, 349},
    {880, 330},
    {874, 349},
    {671, 418},
    {970, 455},
    {1039, 427},
    {1079, 479},
    {1206, 446},
    {1285, 220},
    {1352, 346},
    {1285, 459},
    {1394, 432},
    {1536, 388},
    {1650, 383},
    {1576, 440},
    {1389, 558},
    {1390, 626},
    {1563, 773},
};

Coord REGION_ICON_COORDS[45] = {
    {327, 217},
    {337, 333},
    {288, 432},
    {431, 566},
    {429, 631},
    {613, 629},
    {934, 179},
    {925, 231},
    {861, 234},
    {861, 234},
    {903, 253},
    {891, 272},
    {919, 284},
    {881, 299},
    {826, 325},
    {846, 331},
    {969, 161},
    {968, 179},
    {924, 256},
    {948, 256},
    {937, 269},
    {955, 278},
    {971, 287},
    {992, 284},
    {992, 304},
    {975, 300},
    {1048, 339},
    {989, 331},
    {943, 309},
    {943, 309},
    {772, 412},
    {988, 422},
    {1054, 394},
    {1126, 446},
    {1172, 438},
    {1307, 187},
    {1368, 313},
    {1300, 426},
    {1504, 424},
    {1547, 354},
    {1614, 376},
    {1543, 431},
    {1426, 525},
    {1432, 593},
    {1606, 740}
};

struct _RegionView {
    Karim *karim;
    int w, h;
    struct nemotool *tool;
    struct nemoshow *show;
    NemoWidget *widget;

    NemoWidgetGrab *icon_grab;
    struct showone *group;
    Image *bg;
    List *maps;
    List *icons;
    List *txts;
};

typedef struct _RegionMap RegionMap;
struct _RegionMap {
    int w, h;
    struct nemotool *tool;
    struct nemoshow *show;
    struct showone *group;

    struct showone *one;
    List *ones;

    struct nemotimer *timer;
};

static void _region_map_timeout(struct nemotimer *timer, void *userdata)
{
    RegionMap *map = userdata;
    int delay = 0;
    List *l;
    struct showone *one;
    LIST_FOR_EACH(map->ones, l, one) {
        _nemoshow_item_motion_bounce(one, NEMOEASE_LINEAR_TYPE, 2000, delay,
                "alpha", 1.0, 0.0,
                NULL);
        delay += 250;
    }
    nemotimer_set_timeout(timer, 2000 + delay + 250);
    nemoshow_dispatch_frame(map->show);
}

static RegionMap *region_map_create(RegionView *view, struct showone *parent, const char *uri, int width, int height)
{
    RegionMap *map = calloc(sizeof(RegionMap), 1);
    map->tool = view->tool;
    map->show = view->show;
    map->w = width;
    map->h = height;

    struct showone *one;
    struct showone *group;
    map->group = group = GROUP_CREATE(parent);
    nemoshow_item_set_alpha(group, 0.0);

    char buf[PATH_MAX];
    snprintf(buf, PATH_MAX, "%s%d.svg", uri, 1);
    map->one = one = SVG_PATH_GROUP_CREATE(group, width, height, buf);
    //nemoshow_item_scale(one, 0.5, 0.5);
    //nemoshow_item_translate(one, view->w * (1.0 - 0.5)/2.0, view->h * (1.0 - 0.5)/2.0);

    int i;
    for (i = 2 ; i <= 5 ; i++) {
        char buf[PATH_MAX];
        snprintf(buf, PATH_MAX, "%s%d.svg", uri, i);
        one = SVG_PATH_GROUP_CREATE(group, width, height, buf);
        nemoshow_item_set_fill_color(one, RGBA(0x0));
        nemoshow_item_set_alpha(one, 0.0);
        //nemoshow_item_scale(one, 0.5, 0.5);
        //nemoshow_item_translate(one, view->w * (1.0 - 0.5)/2.0, view->h * (1.0 - 0.5)/2.0);
        map->ones = list_append(map->ones, one);
    }

    map->timer = TOOL_ADD_TIMER(map->tool, 0, _region_map_timeout, map);

    return map;
}

static void region_map_translate(RegionMap *map, uint32_t easetype, int duration, int delay, double tx, double ty)
{
    if (duration > 0) {
        _nemoshow_item_motion(map->group, easetype, duration, delay,
                "tx", tx, "ty", ty,
                NULL);
    } else {
        nemoshow_item_translate(map->group, tx, ty);
    }
}

static void region_map_show(RegionMap *map, uint32_t easetype, int duration, int delay)
{
    _nemoshow_item_motion(map->group, easetype, duration, delay,
            "alpha", 1.0,
            "tx", 0.0, "ty", 0.0,
            NULL);
    nemotimer_set_timeout(map->timer, duration + delay);
}

static void region_map_hide(RegionMap *map, uint32_t easetype, int duration, int delay)
{
    _nemoshow_item_motion(map->group, easetype, duration, delay,
            "alpha", 0.0, NULL);
    nemotimer_set_timeout(map->timer, 0);
}

static void _region_view_grab_event(NemoWidgetGrab *grab, NemoWidget *widget, struct showevent *event, void *userdata)
{
    struct nemoshow *show = nemowidget_get_show(widget);
    double ex, ey;
    nemowidget_transform_from_global(widget,
            nemoshow_event_get_x(event),
            nemoshow_event_get_y(event), &ex, &ey);

    struct showone *one = userdata;
    RegionView *view = nemoshow_one_get_userdata(one);
    uint32_t tag = nemoshow_one_get_tag(one);
    int idx = (int)tag - 1;

    if (nemoshow_event_is_down(show, event)) {
        struct showone *one;
        one = LIST_DATA(list_get_nth(view->icons, idx));
        _nemoshow_item_motion_bounce(one, NEMOEASE_CUBIC_INOUT_TYPE, 500, 0,
                "sx", 2.0, 1.5, "sy", 2.0, 1.5,
                NULL);
        one = LIST_DATA(list_get_nth(view->txts, idx));
        _nemoshow_item_motion_bounce(one, NEMOEASE_CUBIC_INOUT_TYPE, 500, 0,
                "sx", 2.0, 1.5, "sy", 2.0, 1.5,
                NULL);
    } else if (nemoshow_event_is_up(show, event)) {
        struct showone *one;
        one = LIST_DATA(list_get_nth(view->icons, idx));
        _nemoshow_item_motion_bounce(one, NEMOEASE_CUBIC_INOUT_TYPE, 500, 0,
                "sx", 0.5, 1.0, "sy", 0.5, 1.0,
                NULL);
        one = LIST_DATA(list_get_nth(view->txts, idx));
        _nemoshow_item_motion_bounce(one, NEMOEASE_CUBIC_INOUT_TYPE, 500, 0,
                "sx", 0.5, 1.0, "sy", 0.5, 1.0,
                NULL);
        view->icon_grab = NULL;
    }
}

static void _region_view_event(NemoWidget *widget, const char *id, void *info, void *userdata)
{
    struct showevent *event = info;
    RegionView *view = userdata;
    struct nemoshow *show = nemowidget_get_show(widget);

    double ex, ey;
    nemowidget_transform_from_global(widget,
            nemoshow_event_get_x(event),
            nemoshow_event_get_y(event), &ex, &ey);

    if (nemoshow_event_is_down(show, event)) {
        struct showone *one;
        one = nemowidget_pick_one(view->widget, ex, ey);
        if (one && !view->icon_grab) {
            view->icon_grab = nemowidget_create_grab(widget, event,
                    _region_view_grab_event, one);
        }
    }
}

static RegionView *region_view_create(Karim *karim, NemoWidget *parent, int width, int height)
{
    RegionView *view = calloc(sizeof(RegionView), 1);
    view->karim = karim;
    view->tool = nemowidget_get_tool(parent);
    view->show = nemowidget_get_show(parent);
    view->w = width;
    view->h = height;

    NemoWidget *widget;
    view->widget = widget = nemowidget_create_vector(parent, width, height);
    nemowidget_append_callback(widget, "event", _region_view_event, view);
    nemowidget_set_alpha(widget, 0, 0, 0, 0.0);

    struct showone *group;
    struct showone *one;
    view->group = group = GROUP_CREATE(nemowidget_get_canvas(widget));

    // Designed for 3840x2160
    double sx, sy;
    sx = view->w/3840.0;
    sy = view->h/3840.0;
    int w, h;
    const char *uri;
    Image *img;
    uri = KARIM_IMG_DIR"/region/region-BG.png";
    file_get_image_wh(uri, &w, &h);
    w = w * sx;
    h = h * sy;
    view->bg = img = image_create(group);
    image_load_full(img, view->tool, uri, w, h, NULL, NULL);

    int i;
    for (i = 1 ; i <= 25 ; i++) {
        char buf[PATH_MAX];
        snprintf(buf, PATH_MAX, KARIM_ICON_DIR"/region/karim-map%02d-", i);
        RegionMap *map;
        map = region_map_create(view, group, buf, width, height);

        if (1 == i) {
            region_map_translate(map, 0, 0, 0, -width/4, 0);
        } else if (2 <= i && i <= 7) {
            region_map_translate(map, 0, 0, 0, 0, -height/4);
        } else if (6 <= i && i <= 7) {
            region_map_translate(map, 0, 0, 0, width/4, 0);
        } else if (8 <= i && i <= 9) {
            region_map_translate(map, 0, 0, 0, width/2, 0);
        } else if (10 <= i && i <= 12) {
            region_map_translate(map, 0, 0, 0, 0, -height/4);
        } else if (13 == i) {
            region_map_translate(map, 0, 0, 0, 0, height);
        } else if (14 <= i && i <= 20) {
            region_map_translate(map, 0, 0, 0, width, 0);
        } else  {
            region_map_translate(map, 0, 0, 0, 0, height);
        }

        view->maps = list_append(view->maps, map);
    }

    // Designed for 1920x1080
    sx = view->w/1920.0;
    sy = view->h/1080.0;
    for (i = 1 ; i <= 45 ; i++) {
        double w, h;
        char buf[PATH_MAX];
        snprintf(buf, PATH_MAX, KARIM_ICON_DIR"/region/region-icon%02d.svg", i);
        svg_get_wh(buf, &w, &h);
				w *= sx;
				h *= sy;
        one = SVG_PATH_GROUP_CREATE(group, w, h, buf);
        nemoshow_one_set_state(one, NEMOSHOW_PICK_STATE);
        nemoshow_one_set_userdata(one, view);
        nemoshow_one_set_tag(one, i);
        nemoshow_item_set_anchor(one, 0.5, 0.5);
        nemoshow_item_translate(one,
                REGION_ICON_COORDS[i-1].x * sx + w/2,
                REGION_ICON_COORDS[i-1].y * sy + h/2);
        nemoshow_item_set_alpha(one, 0.0);
        nemoshow_item_scale(one, 0.0, 0.0);
        view->icons = list_append(view->icons, one);
    }
    for (i = 1 ; i <= 45 ; i++) {
        double w, h;
        char buf[PATH_MAX];
        snprintf(buf, PATH_MAX, KARIM_ICON_DIR"/region/region-text%02d.svg", i);
        svg_get_wh(buf, &w, &h);
				w *= sx;
				h *= sy;
        one = SVG_PATH_GROUP_CREATE(group, w, h, buf);
        nemoshow_one_set_state(one, NEMOSHOW_PICK_STATE);
        nemoshow_one_set_userdata(one, view);
        nemoshow_one_set_tag(one, i);
        nemoshow_item_set_anchor(one, 0.5, 0.5);
        nemoshow_item_translate(one,
                REGION_TXT_COORDS[i-1].x * sx + w/2,
                REGION_TXT_COORDS[i-1].y * sy + h/2);
        nemoshow_item_set_alpha(one, 0.0);
        nemoshow_item_scale(one, 0.0, 0.0);
        view->txts = list_append(view->txts, one);
    }

    return view;

}

static void region_view_show(RegionView *view, uint32_t easetype, int duration, int delay)
{
    nemowidget_show(view->widget, 0, 0, 0);
    nemowidget_set_alpha(view->widget, easetype, duration, delay, 1.0);

    int i = 1;
    int _delay = 0;
    List *l;
    RegionMap *map;
    LIST_FOR_EACH(view->maps, l, map) {
        region_map_show(map, easetype, duration, delay + _delay);
        _delay += 150;
        i++;
    }

    struct showone *one;
    LIST_FOR_EACH(view->icons, l, one) {
        if (i == 10 || i == 30) {
            i++;
            continue;
        }
        int ddelay;
        if (1 <= i && i <= 6) {
            ddelay = delay + 150 + 50 * i;
        } else if (i == 9) {
            ddelay = delay + 150 * 11;
        } else if (i == 40) {
            ddelay = delay + 150 * 14;
        } else if (i == 41) {
            ddelay = delay + 150 * 15;
        } else if (i == 43) {
            ddelay = delay + 150 * 21;
        } else if (i == 44) {
            ddelay = delay + 150 * 23;
        } else {
            ddelay = delay + 150 * 10 + 50 * (i - 10);
        }
        _nemoshow_item_motion_bounce(one, easetype, duration, ddelay + 500,
                "alpha", 1.0, 1.0,
                "sx", 1.5, 1.0, "sy", 1.5, 1.0,
                NULL);
        i++;
    }

    i = 1;
    LIST_FOR_EACH(view->txts, l, one) {
        int ddelay;
        if (1 <= i && i <= 6) {
            ddelay = delay + 150 + 50 * i;
        } else if (i == 9) {
            ddelay = delay + 150 * 11;
        } else if (i == 40) {
            ddelay = delay + 150 * 14;
        } else if (i == 41) {
            ddelay = delay + 150 * 15;
        } else if (i == 43) {
            ddelay = delay + 150 * 21;
        } else if (i == 44) {
            ddelay = delay + 150 * 23;
        } else {
            ddelay = delay + 150 * 10 + 50 * (i - 10);
        }
        _nemoshow_item_motion_bounce(one, easetype, duration, ddelay + 600,
                "alpha", 1.0, 1.0,
                "sx", 1.5, 1.0, "sy", 1.5, 1.0,
                NULL);
        i++;
    }
    nemoshow_dispatch_frame(view->show);
}

static void region_view_hide(RegionView *view, uint32_t easetype, int duration, int delay)
{
    nemowidget_hide(view->widget, 0, 0, 0);
    nemowidget_set_alpha(view->widget, easetype, duration, delay, 0.0);

    int width = view->h;
    int height = view->w;
    int i = 1;
    List *l;
    RegionMap *map;
    LIST_FOR_EACH(view->maps, l, map) {
        if (1 == i) {
            region_map_translate(map, easetype, duration, delay, -width/4, 0);
        } else if (2 <= i && i <= 7) {
            region_map_translate(map, easetype, duration, delay, 0, -height/4);
        } else if (6 <= i && i <= 7) {
            region_map_translate(map, easetype, duration, delay, width/4, 0);
        } else if (8 <= i && i <= 9) {
            region_map_translate(map, easetype, duration, delay, width/2, 0);
        } else if (10 <= i && i <= 12) {
            region_map_translate(map, easetype, duration, delay, 0, -height/4);
        } else if (13 == i) {
            region_map_translate(map, easetype, duration, delay, 0, height);
        } else if (14 <= i && i <= 20) {
            region_map_translate(map, easetype, duration, delay, width, 0);
        } else  {
            region_map_translate(map, easetype, duration, delay, 0, height);
        }
        region_map_hide(map, easetype, duration, delay);
        i++;
    }
    struct showone *one;
    LIST_FOR_EACH(view->icons, l, one) {
        _nemoshow_item_motion(one, easetype, duration, delay,
                "alpha", 0.0,
                "sx", 0.0, "sy", 0.0,
                NULL);
    }
    LIST_FOR_EACH(view->txts, l, one) {
        _nemoshow_item_motion(one, easetype, duration, delay,
                "alpha", 0.0,
                "sx", 0.0, "sy", 0.0,
                NULL);
    }
    nemoshow_dispatch_frame(view->show);
}

Coord WORK_WAVE_COORDS[5] = {
    {-50, -86},
    {-50, 108},
    {-50, 214},
    {-50, 508},
    {-50, 651}
};

Coord WORK_IMG_COORDS[13] = {
    {800, 390},
    {830, 60},
    {1650, 90},
    {360, 250},
    {1680, 280},
    {1310, 370},
    {1840, 410},
    {380, 620},
    {1050, 530},
    {1620, 630},
    {440, 840},
    {1140, 770},
    {1730, 820}
};

Coord WORK_TXT_COORDS[13] = {
    {729, 430},
    {716, 88},
    {1608, 122},
    {276, 291},
    {1598, 302},
    {1232, 400},
    {1703, 425},
    {283, 649},
    {960, 548},
    {1621, 663},
    {380, 855},
    {1057, 796},
    {1629, 833}
};

typedef struct _WorkWave WorkWave;
typedef struct _WorkIcon WorkIcon;

struct _WorkWave {
    WorkView *view;
    Path *path0, *path1;

    struct showone *group;
    List *ones;
    struct nemotimer *timer;
    int idx;
};

struct _WorkIcon {
    WorkView *view;
    struct showone *group;
    Image *img, *img1;
    struct showone *txt;
    struct nemotimer *timer;
    int idx;
};

struct _WorkView {
    Karim *karim;
    int w, h;
    struct nemotool *tool;
    struct nemoshow *show;
    NemoWidget *widget;

    NemoWidgetGrab *icon_grab;
    struct showone *group;
    struct showone *bg;
    List *waves;
    List *icons;
};

static void _work_icon_timeout(struct nemotimer *timer, void *userdata)
{
    WorkIcon *icon = userdata;
    int duration = 5000;
    image_rotate(icon->img, 0, 0, 0, 0.0);
    image_rotate(icon->img, NEMOEASE_LINEAR_TYPE, duration, 0, 360.0);
    if (icon->img1) {
        image_rotate(icon->img1, 0, 0, 0, 0.0);
        image_rotate(icon->img1, NEMOEASE_LINEAR_TYPE, duration, 0, 360.0);
    }
    nemotimer_set_timeout(timer, duration);
}

static void menu_view_show(MenuView *view, uint32_t easetype, int duration, int delay);
static void menu_view_hide(MenuView *view, uint32_t easetype, int duration, int delay);

static WorkIcon *work_view_create_icon(WorkView *view, const char *id,
        const char *img_uri0, const char *img_uri1, int img_tx, int img_ty,
        const char *txt_uri, int txt_tx, int txt_ty)
{
    WorkIcon *icon = calloc(sizeof(WorkIcon), 1);
    icon->view = view;

    struct showone *group;
    struct showone *one;
    icon->group = group = GROUP_CREATE(view->group);
    nemoshow_item_set_alpha(group, 0.0);

    int w, h;
    Image *img;
    file_get_image_wh(img_uri0, &w, &h);
    w = w * (view->w/3840.0);
    h = h * (view->h/2160.0);
    icon->img = img = image_create(group);
    image_set_anchor(img, 0.5, 0.5);
    nemoshow_one_set_state(image_get_one(img), NEMOSHOW_PICK_STATE);
    if (id) nemoshow_one_set_id(image_get_one(img), id);
    nemoshow_one_set_userdata(image_get_one(img), icon);
    image_translate(img, 0, 0, 0, img_tx, img_ty);
    image_scale(img, 0, 0, 0, 0, 0);
    image_set_alpha(img, 0, 0, 0, 0.0);
    image_load_full(img, view->tool, img_uri0, w, h, NULL, NULL);
    if (img_uri1) {
        file_get_image_wh(img_uri1, &w, &h);
        w = w * (view->w/3840.0);
        h = h * (view->h/2160.0);
        icon->img1 = img = image_create(group);
        image_set_anchor(img, 0.5, 0.5);
        image_translate(img, 0, 0, 0, img_tx, img_ty);
        image_scale(img, 0, 0, 0, 0, 0);
        image_set_alpha(img, 0, 0, 0, 0.0);
        image_load_full(img, view->tool, img_uri1, w, h, NULL, NULL);
    }

    double ww, hh;
    svg_get_wh(txt_uri, &ww, &hh);
    icon->txt = one = SVG_PATH_GROUP_CREATE(group, ww, hh, txt_uri);
    nemoshow_item_set_anchor(one, 0.5, 0.5);
    nemoshow_item_translate(one, txt_tx + ww/2, txt_ty + hh/2);
    nemoshow_item_scale(one, 0.0, 1.0);

    icon->timer = TOOL_ADD_TIMER(view->tool, 0, _work_icon_timeout, icon);

    return icon;
}

static void work_icon_show(WorkIcon *icon, uint32_t easetype, int duration, int delay)
{
    nemotimer_set_timeout(icon->timer, 10 + delay);
    _nemoshow_item_motion(icon->group, easetype, duration, delay,
            "alpha", 1.0, NULL);

    image_set_alpha(icon->img, easetype, duration, delay, 1.0);
    _nemoshow_item_motion_bounce(image_get_group(icon->img), easetype, duration, delay,
            "sx", 1.25, 1.0, "sy", 1.25, 1.0, NULL);
    _nemoshow_item_motion_bounce(icon->txt, easetype, duration, delay,
            "sx", 1.25, 1.0, NULL);
}

static void work_icon_hide(WorkIcon *icon, uint32_t easetype, int duration, int delay)
{
    nemotimer_set_timeout(icon->timer, 0);
    _nemoshow_item_motion(icon->group, easetype, duration, delay,
            "alpha", 0.0, NULL);

    image_set_alpha(icon->img, easetype, duration, delay, 0.0);
    if (icon->img1) image_set_alpha(icon->img1, easetype, duration, delay, 0.0);
    image_scale(icon->img, easetype, duration, delay, 0.0, 0.0);
    _nemoshow_item_motion_bounce(icon->txt, easetype, duration, delay,
            "sx", 1.5, 1.0, NULL);
}

static void _work_wave_timeout(struct nemotimer *timer, void *userdata)
{
    WorkWave *wave = userdata;
    int cnt = list_count(wave->ones);

    int duration = 5000;
    uint32_t easetype = NEMOEASE_SINUSOIDAL_INOUT_TYPE;

#if 0
    struct showone *one;
    one = LIST_DATA(list_get_nth(wave->ones, wave->idx%cnt));
    //nemoshow_item_set_alpha(one, 0.0);
    _nemoshow_item_motion(one, easetype, 1000, 0,
            "alpha", 1.0, NULL);

    if (wave->idx < cnt) {
        path_array_morph(one, 0, 0, 0, wave->path0);
        path_array_morph(one, easetype, duration, 0, wave->path1);
        _nemoshow_item_motion(one, easetype, duration, 0,
                "tx", 50.0, NULL);
    } else {
        path_array_morph(one, 0, 0, 0, wave->path1);
        path_array_morph(one, easetype, duration, 0, wave->path0);
        _nemoshow_item_motion(one, easetype, duration, 0,
                "tx", 0.0, NULL);
    }

    wave->idx++;
    if (wave->idx >= cnt * 2) wave->idx = 0;

    nemotimer_set_timeout(wave->timer, duration/cnt);
    nemoshow_dispatch_frame(wave->view->show);
#else
    struct showone *one;
    one = LIST_DATA(list_get_nth(wave->ones, wave->idx));
    nemoshow_item_set_alpha(one, 0.0);
    _nemoshow_item_motion(one, easetype, 1000, 0,
            "alpha", 1.0, NULL);

    path_array_morph(one, 0, 0, 0, wave->path0);
    path_array_morph(one, easetype, duration, 0, wave->path1);

    wave->idx++;
    if (wave->idx >= cnt) wave->idx = 0;

    nemotimer_set_timeout(wave->timer, duration/cnt);
    nemoshow_dispatch_frame(wave->view->show);
#endif
}

WorkWave *work_view_create_wave(WorkView *view, const char *uri0, const char *uri1)
{
    WorkWave *wave = calloc(sizeof(WorkWave), 1);
    wave->view = view;

    struct showone *one;
    struct showone *group;
    wave->group = group = GROUP_CREATE(view->group);
    nemoshow_item_set_alpha(group, 0.0);

    wave->path0 = svg_get_path(uri0);
    wave->path1 = svg_get_path(uri1);

    wave->path1->fill &= 0xFFFFFF00;
    wave->path1->stroke &= 0xFFFFFF00;

    double sx, sy;
    sx = view->w/1920.0;
    sy = view->h/1080.0;

    int cnt = 10;
    int i;
    for (i = 0 ; i < cnt ; i++) {
        one = path_draw_array(wave->path0, group);
        nemoshow_item_scale(one, sx, sy);
        nemoshow_item_set_alpha(one, 0.0);
        wave->ones = list_append(wave->ones, one);
    }

    wave->timer = TOOL_ADD_TIMER(view->tool, 0, _work_wave_timeout, wave);

    return wave;
}

static void work_wave_translate(WorkWave *wave, uint32_t easetype, int duration, int delay, double tx, double ty)
{
    if (duration > 0) {
        _nemoshow_item_motion(wave->group, easetype, duration, delay,
                "tx", tx, "ty", ty,
                NULL);
    } else {
        nemoshow_item_translate(wave->group, tx, ty);
    }
}

static void work_wave_scale(WorkWave *wave, uint32_t easetype, int duration, int delay, double sx, double sy)
{
    if (duration > 0) {
        _nemoshow_item_motion(wave->group, easetype, duration, delay,
                "sx", sx, "sy", sy,
                NULL);
    } else {
        nemoshow_item_scale(wave->group, sx, sy);
    }
}

static void work_wave_show(WorkWave *wave, uint32_t easetype, int duration, int delay)
{
    wave->idx = 0;
    nemotimer_set_timeout(wave->timer, 10 + delay);
    if (duration > 0) {
        _nemoshow_item_motion(wave->group, easetype, duration, delay,
                "alpha", 1.0,
                NULL);
    } else {
        nemoshow_item_set_alpha(wave->group, 1.0);
    }
}

static void work_wave_hide(WorkWave *wave, uint32_t easetype, int duration, int delay)
{
    nemotimer_set_timeout(wave->timer, 0);
    if (duration > 0) {
        _nemoshow_item_motion(wave->group, easetype, duration, delay,
                "alpha", 0.0,
                NULL);
    } else {
        nemoshow_item_set_alpha(wave->group, 0.0);
    }
    List *l;
    struct showone *one;
    LIST_FOR_EACH(wave->ones, l, one) {
         path_array_morph(one, easetype, duration, delay, wave->path0);
    }
}

void work_view_show(WorkView *view, uint32_t easetype, int duration, int delay)
{
    nemowidget_show(view->widget, 0, 0, 0);
    nemowidget_set_alpha(view->widget, easetype, duration, delay, 1.0);

    // Designed for 1920x1080
    double sx= view->w/1920.0;
    double sy = view->h/1080.0;
    List *l;
    WorkWave *wave;
    int _delay = 0;
    int i = 1;
    LIST_FOR_EACH(view->waves, l, wave) {
        if (i%2 == 0) {
            work_wave_translate(wave, NEMOEASE_CUBIC_INOUT_TYPE, 1500, delay + _delay,
                    WORK_WAVE_COORDS[i-1].x * sx,
                    WORK_WAVE_COORDS[i-1].y * sy);
        }
        work_wave_scale(wave, NEMOEASE_CUBIC_INOUT_TYPE, 1500, delay + _delay, 1.0, 1.0);
        work_wave_show(wave, NEMOEASE_CUBIC_INOUT_TYPE, 1500, delay + _delay);
        _delay += 250;
        i++;
    }
    _delay = 0;
    WorkIcon *icon;
    LIST_FOR_EACH(view->icons, l, icon) {
        work_icon_show(icon, NEMOEASE_CUBIC_OUT_TYPE, 1000, delay + _delay);
        _delay += 200;
    }
}

void work_view_hide(WorkView *view, uint32_t easetype, int duration, int delay)
{
    nemowidget_hide(view->widget, 0, 0, 0);
    nemowidget_set_alpha(view->widget, easetype, duration, delay, 0.0);

    // Designed for 1920x1080
    double sx= view->w/1920.0;
    double sy = view->h/1080.0;
    int i = 1;
    List *l;
    WorkWave *wave;
    LIST_FOR_EACH(view->waves, l, wave) {
        if (i%2 == 0) {
            work_wave_translate(wave, NEMOEASE_CUBIC_INOUT_TYPE, 1500, delay,
                    WORK_WAVE_COORDS[i-1].x * sx + view->w,
                    WORK_WAVE_COORDS[i-1].y * sy);
        } else {
            work_wave_translate(wave, NEMOEASE_CUBIC_INOUT_TYPE, 1500, delay,
                    WORK_WAVE_COORDS[i-1].x * sx,
                    WORK_WAVE_COORDS[i-1].y * sy);
        }
        work_wave_scale(wave, NEMOEASE_CUBIC_INOUT_TYPE, 1500, delay, 0.0, 1.0);
        work_wave_hide(wave, NEMOEASE_CUBIC_INOUT_TYPE, 1500, delay);
        i++;
    }
    WorkIcon *icon;
    LIST_FOR_EACH(view->icons, l, icon) {
        work_icon_hide(icon, NEMOEASE_CUBIC_IN_TYPE, 500, 0);
    }
}

static void work_icon_down(WorkIcon *icon, uint32_t easetype, int duration, int delay)
{
    _nemoshow_item_motion_bounce(image_get_group(icon->img), easetype, duration, delay,
            "sx", 1.5, 1.4, "sy", 1.5, 1.4,
            NULL);
    if (icon->img1) {
        _nemoshow_item_motion_bounce(image_get_group(icon->img1), easetype, duration, delay,
                "sx", 1.5, 1.4, "sy", 1.5, 1.4,
                NULL);
        image_set_alpha(icon->img, easetype, duration, delay, 0.0);
        image_set_alpha(icon->img1, easetype, duration, delay, 1.0);
    }
}

static void work_icon_up(WorkIcon *icon, uint32_t easetype, int duration, int delay)
{
    _nemoshow_item_motion_bounce(image_get_group(icon->img), easetype, duration, delay,
            "sx", 0.8, 1.0, "sy", 0.8, 1.0,
            NULL);
    if (icon->img1) {
        _nemoshow_item_motion_bounce(image_get_group(icon->img1), easetype, duration, delay,
                "sx", 0.8, 1.0, "sy", 0.8, 1.0,
                NULL);
        image_set_alpha(icon->img, easetype, duration, delay, 1.0);
        image_set_alpha(icon->img1, easetype, duration, delay, 0.0);
    }
}

static void _work_view_grab_event(NemoWidgetGrab *grab, NemoWidget *widget, struct showevent *event, void *userdata)
{
    struct nemoshow *show = nemowidget_get_show(widget);
    double ex, ey;
    nemowidget_transform_from_global(widget,
            nemoshow_event_get_x(event),
            nemoshow_event_get_y(event), &ex, &ey);

    struct showone *one = userdata;
    const char *id = nemoshow_one_get_id(one);
    WorkIcon *icon = nemoshow_one_get_userdata(one);
    WorkView *view = icon->view;
    RET_IF(!id);

    if (nemoshow_event_is_down(show, event)) {
        work_icon_down(icon, NEMOEASE_CUBIC_INOUT_TYPE, 250, 0);
        nemoshow_dispatch_frame(view->show);
    } else if (nemoshow_event_is_up(show, event)) {
        work_icon_up(icon, NEMOEASE_CUBIC_INOUT_TYPE, 250, 0);
        view->icon_grab = NULL;
        if (nemoshow_event_is_single_click(show, event)) {
            if (!strcmp(id, "tabletop")) {
                Karim *karim = view->karim;
                karim->type = KARIM_TYPE_HONEY;
                karim->honey = honey_view_create(karim, karim->parent, karim->w, karim->h);
                honey_view_show(karim->honey, NEMOEASE_CUBIC_OUT_TYPE, 1500, 0);
                work_view_hide(karim->work, NEMOEASE_CUBIC_IN_TYPE, 1000, 0);
                menu_view_hide(karim->menu, NEMOEASE_CUBIC_IN_TYPE, 1000, 0);
            }
        }
        nemoshow_dispatch_frame(view->show);
    }
}

static void _work_view_event(NemoWidget *widget, const char *id, void *info, void *userdata)
{
    struct showevent *event = info;
    WorkView *view = userdata;
    struct nemoshow *show = nemowidget_get_show(widget);

    double ex, ey;
    nemowidget_transform_from_global(widget,
            nemoshow_event_get_x(event),
            nemoshow_event_get_y(event), &ex, &ey);

    if (nemoshow_event_is_down(show, event)) {
        struct showone *one;
        one = nemowidget_pick_one(view->widget, ex, ey);
        if (one && !view->icon_grab) {
            view->icon_grab = nemowidget_create_grab(widget, event,
                    _work_view_grab_event, one);
        }
    }
}

WorkView *work_view_create(Karim *karim, NemoWidget *parent, int width, int height)
{
    WorkView *view = calloc(sizeof(WorkView), 1);
    view->karim = karim;
    view->tool = nemowidget_get_tool(parent);
    view->show = nemowidget_get_show(parent);
    view->w = width;
    view->h = height;

    NemoWidget *widget;
    view->widget = widget = nemowidget_create_vector(parent, width, height);
    nemowidget_append_callback(widget, "event", _work_view_event, view);
    nemowidget_set_alpha(widget, 0, 0, 0, 0.0);

    struct showone *group;
    struct showone *one;
    view->group = group = GROUP_CREATE(nemowidget_get_canvas(widget));

    view->bg = one = RECT_CREATE(group, width, height);
    nemoshow_item_set_fill_color(one, RGBA(0xF8F6F3FF));

    // Designed for 1920x1080
    double sx= view->w/1920.0;
    double sy = view->h/1080.0;
    int i;
    for (i = 1 ; i <= 5; i++) {
        char buf0[PATH_MAX], buf1[PATH_MAX];
        snprintf(buf0, PATH_MAX, KARIM_ICON_DIR"/work/karim-workwave-%d-1.svg", i);
        snprintf(buf1, PATH_MAX, KARIM_ICON_DIR"/work/karim-workwave-%d-2.svg", i);
        WorkWave *wave;
        if (i%2 == 0) {
            wave = work_view_create_wave(view, buf1, buf0);
        } else {
            wave = work_view_create_wave(view, buf0, buf1);
        }
        if (i%2 == 0) {
            work_wave_translate(wave, 0, 0, 0,
                    WORK_WAVE_COORDS[i-1].x * sx + view->w,
                    WORK_WAVE_COORDS[i-1].y * sy);
        } else {
            work_wave_translate(wave, 0, 0, 0,
                    WORK_WAVE_COORDS[i-1].x * sx,
                    WORK_WAVE_COORDS[i-1].y * sy);
        }
        work_wave_scale(wave, 0, 0, 0, 0.0, 1.0);
        view->waves = list_append(view->waves, wave);
    }

    for (i = 1 ; i <= 13 ; i++) {
        char buf0[PATH_MAX], buf1[PATH_MAX], buf2[PATH_MAX];
        snprintf(buf0, PATH_MAX, KARIM_IMG_DIR"/work/icon-%02d.png", i);
        snprintf(buf1, PATH_MAX, KARIM_IMG_DIR"/work/icon-%02d-1.png", i);
        snprintf(buf2, PATH_MAX, KARIM_ICON_DIR"/work/text/work-text%02d.svg", i);
        WorkIcon *icon;
        if (i == 1) {
            icon = work_view_create_icon(view, "tabletop",
                    buf0, buf1,
                    WORK_IMG_COORDS[i-1].x * sx,
                    WORK_IMG_COORDS[i-1].y * sy,
                    buf2,
                    WORK_TXT_COORDS[i-1].x * sx,
                    WORK_TXT_COORDS[i-1].y * sy);
        } else {
            icon = work_view_create_icon(view, NULL,
                    buf0, buf1,
                    WORK_IMG_COORDS[i-1].x * sx,
                    WORK_IMG_COORDS[i-1].y * sy,
                    buf2,
                    WORK_TXT_COORDS[i-1].x * sx,
                    WORK_TXT_COORDS[i-1].y * sy);
        }
        view->icons = list_append(view->icons, icon);
    }

    return view;
}

struct _MenuView {
    Karim *karim;
    int w, h;
    struct nemotool *tool;
    struct nemoshow *show;
    NemoWidget *widget;

    NemoWidgetGrab *btn_grab;
    struct showone *group;

    struct nemotimer *timer;
    struct showone *out;
    List *outs;
    struct showone *btn_region;
    struct showone *btn_wave;
    struct showone *btn_year;
};

static void _menu_view_timeout(struct nemotimer *timer, void *userdata)
{
    MenuView *view = userdata;
    int duration = 1500;
    int delay = 0;
    List *l;
    struct showone *one;
    LIST_FOR_EACH(view->outs, l, one) {
        _nemoshow_item_motion_bounce(one, NEMOEASE_LINEAR_TYPE, duration, delay,
                "alpha", 1.0, 0.0,
                NULL);
        nemoshow_item_scale(one, 0.8, 0.8);
        _nemoshow_item_motion(one, NEMOEASE_LINEAR_TYPE, duration, delay,
                "sx", 1.25,
                "sy", 1.25,
                NULL);
        delay += 250;
    }
    nemotimer_set_timeout(timer, duration + delay + 250);
    nemoshow_dispatch_frame(view->show);
}

static void _menu_view_grab_event(NemoWidgetGrab *grab, NemoWidget *widget, struct showevent *event, void *userdata)
{
    struct nemoshow *show = nemowidget_get_show(widget);
    double ex, ey;
    nemowidget_transform_from_global(widget,
            nemoshow_event_get_x(event),
            nemoshow_event_get_y(event), &ex, &ey);

    struct showone *one = userdata;
    const char *id = nemoshow_one_get_id(one);
    MenuView *view = nemoshow_one_get_userdata(one);
    RET_IF(!id);

    if (nemoshow_event_is_down(show, event)) {
        if (!strcmp(id, "region")) {
            _nemoshow_item_motion_bounce(view->btn_region, NEMOEASE_CUBIC_INOUT_TYPE, 250, 0,
                    "sx", 1.5, 1.4, "sy", 1.5, 1.4,
                    NULL);
        } else if (!strcmp(id, "work")) {
            _nemoshow_item_motion_bounce(view->btn_wave, NEMOEASE_CUBIC_INOUT_TYPE, 250, 0,
                    "sx", 1.5, 1.4, "sy", 1.5, 1.4,
                    NULL);
        } else if (!strcmp(id, "year")) {
            _nemoshow_item_motion_bounce(view->btn_year, NEMOEASE_CUBIC_INOUT_TYPE, 250, 0,
                    "sx", 1.5, 1.4, "sy", 1.5, 1.4,
                    NULL);
        }
        nemoshow_dispatch_frame(view->show);
    } else if (nemoshow_event_is_up(show, event)) {
        view->btn_grab = NULL;
        if (nemoshow_event_is_single_click(show, event)) {
            double sx, sy;
            sx = view->w/1920.0;
            sy = view->h/1080.0;
            double gw = 440 * sx;
            double gh = 140 * sy;
            _nemoshow_item_motion(view->group, NEMOEASE_CUBIC_OUT_TYPE, 1000, 0,
                    "alpha", 1.0,
                    "sx", 1.0, "sy", 1.0,
                    "tx", view->w/2.0 + gw/2, "ty", view->h * 0.9 + gh/2,
                    NULL);

            uint32_t color0 = 0xE70095FF;
            uint32_t color1 = 0xF7ACB87F;
            if (!strcmp(id, "region")) {
                _nemoshow_item_motion_bounce(view->btn_region, NEMOEASE_CUBIC_INOUT_TYPE, 500, 0,
                        "sx", 0.8, 1.0, "sy", 0.8, 1.0,
                        NULL);
                _nemoshow_item_motion(view->btn_region, NEMOEASE_CUBIC_INOUT_TYPE, 500, 0,
                        "fill", color0,
                        NULL);
                _nemoshow_item_motion(view->btn_wave, NEMOEASE_CUBIC_INOUT_TYPE, 500, 0,
                        "fill", color1,
                        NULL);
                _nemoshow_item_motion(view->btn_year, NEMOEASE_CUBIC_INOUT_TYPE, 500, 0,
                        "fill", color1,
                        NULL);
            } else if (!strcmp(id, "work")) {
                _nemoshow_item_motion_bounce(view->btn_wave, NEMOEASE_CUBIC_INOUT_TYPE, 500, 0,
                        "sx", 0.8, 1.0, "sy", 0.8, 1.0,
                        NULL);
                _nemoshow_item_motion(view->btn_region, NEMOEASE_CUBIC_INOUT_TYPE, 500, 0,
                        "fill", color1,
                        NULL);
                _nemoshow_item_motion(view->btn_wave, NEMOEASE_CUBIC_INOUT_TYPE, 500, 0,
                        "fill", color0,
                        NULL);
                _nemoshow_item_motion(view->btn_year, NEMOEASE_CUBIC_INOUT_TYPE, 500, 0,
                        "fill", color1,
                        NULL);
            } else if (!strcmp(id, "year")) {
                _nemoshow_item_motion_bounce(view->btn_year, NEMOEASE_CUBIC_INOUT_TYPE, 500, 0,
                        "sx", 0.8, 1.0, "sy", 0.8, 1.0,
                        NULL);
                _nemoshow_item_motion(view->btn_region, NEMOEASE_CUBIC_INOUT_TYPE, 500, 0,
                        "fill", color1,
                        NULL);
                _nemoshow_item_motion(view->btn_wave, NEMOEASE_CUBIC_INOUT_TYPE, 500, 0,
                        "fill", color1,
                        NULL);
                _nemoshow_item_motion(view->btn_year, NEMOEASE_CUBIC_INOUT_TYPE, 500, 0,
                        "fill", color0,
                        NULL);
            }
            Karim *karim = view->karim;

            if (karim->type == KARIM_TYPE_REGION) {
                region_view_hide(karim->region, NEMOEASE_CUBIC_IN_TYPE, 1000, 0);
            } else if (karim->type == KARIM_TYPE_WORK) {
                work_view_hide(karim->work, NEMOEASE_CUBIC_IN_TYPE, 1000, 0);
            } else if (karim->type == KARIM_TYPE_YEAR) {
                year_view_hide(karim->year, NEMOEASE_CUBIC_INOUT_TYPE, 1000, 0);
            }
            if (!strcmp(id, "region")) {
                karim->type = KARIM_TYPE_REGION;
                region_view_show(karim->region, NEMOEASE_CUBIC_OUT_TYPE, 1000, 500);
            } else if (!strcmp(id, "work")) {
                karim->type = KARIM_TYPE_WORK;
                work_view_show(karim->work, NEMOEASE_CUBIC_OUT_TYPE, 1000, 500);
            } else if (!strcmp(id, "year")) {
                karim->type = KARIM_TYPE_YEAR;
                year_view_show(karim->year, NEMOEASE_CUBIC_OUT_TYPE, 1000, 500);
            }
        } else {
            if (!strcmp(id, "region")) {
                _nemoshow_item_motion_bounce(view->btn_region, NEMOEASE_CUBIC_INOUT_TYPE, 500, 0,
                        "sx", 0.8, 1.0, "sy", 0.8, 1.0,
                        NULL);
            } else if (!strcmp(id, "work")) {
                _nemoshow_item_motion_bounce(view->btn_wave, NEMOEASE_CUBIC_INOUT_TYPE, 500, 0,
                        "sx", 0.8, 1.0, "sy", 0.8, 1.0,
                        NULL);
            } else if (!strcmp(id, "year")) {
                _nemoshow_item_motion_bounce(view->btn_year, NEMOEASE_CUBIC_INOUT_TYPE, 500, 0,
                        "sx", 0.8, 1.0, "sy", 0.8, 1.0,
                        NULL);
            }
        }

        nemoshow_dispatch_frame(view->show);
    }
}

static void _menu_view_event(NemoWidget *widget, const char *id, void *info, void *userdata)
{
    struct showevent *event = info;
    MenuView *view = userdata;
    struct nemoshow *show = nemowidget_get_show(widget);

    double ex, ey;
    nemowidget_transform_from_global(widget,
            nemoshow_event_get_x(event),
            nemoshow_event_get_y(event), &ex, &ey);

    if (nemoshow_event_is_down(show, event)) {
        struct showone *one;
        one = nemowidget_pick_one(view->widget, ex, ey);
        if (one && !view->btn_grab) {
            view->btn_grab = nemowidget_create_grab(widget, event,
                    _menu_view_grab_event, one);
        }
    }
}

static MenuView *menu_view_create(Karim *karim, NemoWidget *parent, int width, int height)
{
    MenuView *view = calloc(sizeof(MenuView), 1);
    view->karim = karim;
    view->tool = nemowidget_get_tool(parent);
    view->show = nemowidget_get_show(parent);
    view->w = width;
    view->h = height;

    NemoWidget *widget;
    view->widget = widget = nemowidget_create_vector(parent, width, height);
    nemowidget_enable_event_repeat(widget, true);
    nemowidget_append_callback(widget, "event", _menu_view_event, view);

    double sx, sy;
    sx = view->w/1920.0;
    sy = view->h/1080.0;
    double gw = 440 * sx;
    double gh = 140 * sy;

    struct showone *group;
    struct showone *one;
    view->group = group = GROUP_CREATE(nemowidget_get_canvas(widget));
    nemoshow_item_set_width(group, gw);
    nemoshow_item_set_height(group, gh);
    nemoshow_item_set_anchor(group, 0.5, 0.5);
    nemoshow_item_set_alpha(group, 0.0);
    nemoshow_item_translate(group, width/2 + gw/2, height * 0.9 + gh/2);

    double stroke_alpha = 0.88;
    double fill_alpha = 0.5;
    int i;
    for (i = 2; i <= 7 ; i++) {
        char uri[PATH_MAX];
        snprintf(uri, PATH_MAX, "%s%d.svg", KARIM_ICON_DIR"/menu/karim-buttonoutline-", i);
        double w, h;
        svg_get_wh(uri, &w, &h);
        w = w * sx;
        h = h * sx;
        one = SVG_PATH_GROUP_CREATE(group, w, h, uri);
        nemoshow_item_set_fill_color(one, 179, 179, 179, 255.0 * stroke_alpha);
        stroke_alpha -= 0.11;
        nemoshow_item_set_fill_color(one, 255, 255, 255, 255.0 * fill_alpha);
        fill_alpha -= 0.07;
        nemoshow_item_set_anchor(one, 0.5, 0.5);
        nemoshow_item_scale(one, 0.8, 0.8);
        nemoshow_item_set_alpha(one, 0.0);
        view->outs = list_append(view->outs, one);
    }

    double w, h;
    const char *uri;
    uri =  KARIM_ICON_DIR"/menu/karim-buttonoutline-1.svg";
    svg_get_wh(uri, &w, &h);
    w = w * sx;
    h = h * sx;
    view->out = one = SVG_PATH_GROUP_CREATE(group, w, h, uri);
    nemoshow_item_set_anchor(one, 0.5, 0.5);

    double ww, hh;
    uri =  KARIM_ICON_DIR"/menu/karim-button-1.svg";
    svg_get_wh(uri, &ww, &hh);
    ww = ww * sx;
    hh = hh * sx;
    view->btn_region = one = SVG_PATH_GROUP_CREATE(group, ww, hh, uri);
    nemoshow_one_set_state(one, NEMOSHOW_PICK_STATE);
    nemoshow_one_set_id(one, "region");
    nemoshow_one_set_userdata(one, view);
    nemoshow_item_set_anchor(one, 0.5, 0.5);
    nemoshow_item_translate(one, 53 * sx - (w - ww)/2, 25 * sy - (h - hh)/2);
    nemoshow_item_set_fill_color(one, RGBA(0xF7ACB87F));

    uri =  KARIM_ICON_DIR"/menu/karim-button-2.svg";
    svg_get_wh(uri, &ww, &hh);
    ww = ww * sx;
    hh = hh * sx;
    view->btn_wave = one = SVG_PATH_GROUP_CREATE(group, ww, hh, uri);
    nemoshow_one_set_state(one, NEMOSHOW_PICK_STATE);
    nemoshow_one_set_id(one, "work");
    nemoshow_one_set_userdata(one, view);
    nemoshow_item_set_anchor(one, 0.5, 0.5);
    nemoshow_item_translate(one, 175 * sx - (w - ww)/2, 25 * sy - (h - hh)/2);
    nemoshow_item_set_fill_color(one, RGBA(0xF7ACB87F));

    uri =  KARIM_ICON_DIR"/menu/karim-button-3.svg";
    svg_get_wh(uri, &ww, &hh);
    ww = ww * sx;
    hh = hh * sx;
    view->btn_year = one = SVG_PATH_GROUP_CREATE(group, ww, hh, uri);
    nemoshow_one_set_state(one, NEMOSHOW_PICK_STATE);
    nemoshow_one_set_id(one, "year");
    nemoshow_one_set_userdata(one, view);
    nemoshow_item_set_anchor(one, 0.5, 0.5);
    nemoshow_item_translate(one, 297 * sx - (w - ww)/2, 25 * sy - (h - hh)/2);
    nemoshow_item_set_fill_color(one, RGBA(0xF7ACB87F));

    view->timer = TOOL_ADD_TIMER(view->tool, 0, _menu_view_timeout, view);

    return view;
}

static void menu_view_show(MenuView *view, uint32_t easetype, int duration, int delay)
{
    double sx, sy;
    sx = view->w/1920.0;
    sy = view->h/1080.0;
    double gw = 440 * sx;
    double gh = 140 * sy;

    nemowidget_show(view->widget, 0, 0, 0);
    nemowidget_set_alpha(view->widget, easetype, duration, delay, 1.0);
    _nemoshow_item_motion(view->group, easetype, duration, delay,
            "alpha", 1.0,
            "sx", 1.5, "sy", 1.5,
            "tx", view->w/2.0 + gw/2*1.5, "ty", view->h/2.0 + gh/2*1.5,
            NULL);
    nemoshow_dispatch_frame(view->show);
    nemotimer_set_timeout(view->timer, duration + delay);
}

static void menu_view_hide(MenuView *view, uint32_t easetype, int duration, int delay)
{
    /*
    double sx, sy;
    sx = view->w/1920.0;
    sy = view->h/1080.0;
    double gw = 440 * sx;
    double gh = 140 * sy;
    */

    nemowidget_hide(view->widget, 0, 0, 0);
    nemowidget_set_alpha(view->widget, easetype, duration, delay, 0.0);
    _nemoshow_item_motion(view->group, easetype, duration, delay,
            "alpha", 0.0,
            /*
            "sx", 1.5, "sy", 1.5,
            "tx", view->w/2.0 + gw/2*1.5, "ty", view->h/2.0 + gh/2*1.5,
            */
            NULL);
    nemoshow_dispatch_frame(view->show);
    nemotimer_set_timeout(view->timer, 0);
}

typedef struct _IntroGrab IntroGrab;
struct _IntroGrab {
    int cnt;
    uint32_t device;
};

struct _IntroView {
    Karim *karim;
    int w, h;
    struct nemotool *tool;
    struct nemoshow *show;
    NemoWidget *widget;
    double scale;

    Path *path0, *path1;
    List *paths;

    struct showone *group;
    struct showone *bg;
    struct showone *logo;
    NemoWidgetGrab *logo_grab;
    List *grabs;

    List *effects;
    int idx;

    List *touch_effects;

    struct nemotimer *timer;
};

typedef struct _IntroEffect IntroEffect;
struct _IntroEffect {
    IntroView *view;
    unsigned long time;
    List *ones;
};

static void intro_effect_destroy(IntroEffect *fx)
{
    struct showone *one;
    LIST_FREE(fx->ones, one) {
        nemoshow_one_destroy(one);
    }
}

static void intro_effect_hide(IntroEffect *fx, uint32_t easetype, int duration, int delay)
{
    List *l;
    struct showone *one;
    int _delay = 0;
    LIST_FOR_EACH_REVERSE(fx->ones, l, one) {
        _nemoshow_item_motion(one, easetype, duration, delay + _delay,
                "alpha", 0.0, "sx", fx->view->scale, "sy", fx->view->scale,
                NULL);
        _delay += 50;
    }
}

static void intro_view_destroy(IntroView *view)
{
    nemotimer_destroy(view->timer);

    Path *path;
    LIST_FREE(view->paths, path) path_destroy(path);
    path_destroy(view->path0);
    path_destroy(view->path1);

    IntroEffect *fx;
    LIST_FREE(view->effects, fx) {
        intro_effect_destroy(fx);
    }

    nemoshow_one_destroy(view->group);
    nemowidget_destroy(view->widget);
    free(view);
}

static void intro_view_show(IntroView *view, uint32_t easetype, int duration, int delay)
{
    view->idx = 0;
    nemowidget_show(view->widget, 0, 0, 0);
    nemowidget_set_alpha(view->widget, NEMOEASE_CUBIC_OUT_TYPE, 1000, 0, 1.0);
    nemotimer_set_timeout(view->timer, 10);
    nemoshow_dispatch_frame(view->show);
}

static void intro_view_hide(IntroView *view, uint32_t easetype, int duration, int delay)
{
    IntroGrab *grab;
    LIST_FREE(view->grabs, grab) {
        free(grab);
    }
    nemowidget_hide(view->widget, 0, 0, 0);
    nemowidget_set_alpha(view->widget, easetype, duration, delay, 0.0);

    _nemoshow_item_motion(view->logo, easetype, duration/2, delay,
            "sx", 0.0, "sy", 0.0,
            NULL);

    int cnt = list_count(view->effects);
    int idx = view->idx - 1;
    if (idx < 0) {
        idx = cnt - 1;
    }
    int _delay = delay;
    do {
        IntroEffect *fx = LIST_DATA(list_get_nth(view->effects, idx));
        intro_effect_hide(fx, NEMOEASE_CUBIC_IN_TYPE, duration/cnt, _delay);
        _delay += 150;

        if (idx == view->idx) break;
        idx--;
        if (idx < 0) {
            idx = list_count(view->effects) - 1;
        }
    } while (1);

    nemotimer_set_timeout(view->timer, 0);
    nemoshow_dispatch_frame(view->show);
}

static void _intro_view_timeout(struct nemotimer *timer, void *userdata)
{
    IntroView *view = userdata;

    if (!view->logo_grab) {
        _nemoshow_item_motion_bounce(view->logo, NEMOEASE_CUBIC_INOUT_TYPE, 1000, 0,
                "sx", 1.2, 1.0, "sy", 1.2, 1.0,
                "alpha", 0.5, 1.0,
                NULL);
    }

    IntroEffect *fx = LIST_DATA(list_get_nth(view->effects, view->idx));
    view->idx++;
    if (view->idx >= list_count(view->effects)) {
        view->idx = 0;
    }

    int delay = 0;
    List *l;
    struct showone *one;
    LIST_FOR_EACH_REVERSE(fx->ones, l, one) {
        nemoshow_item_scale(one, 0.0, 0.0);
        nemoshow_item_set_alpha(one, 0.5);
        nemoshow_item_rotate(one, 0.0);
        _nemoshow_item_motion(one, NEMOEASE_LINEAR_TYPE, 20000, delay,
                "sx", view->scale, "sy", view->scale,
                "alpha", 1.0,
                "ro", 270.0,
                NULL);
        delay += 20;
    }

    List *ll;
    LIST_FOR_EACH_SAFE(view->touch_effects, l, ll, fx) {
        if (fx->time + 6 <= time(NULL)) {
            view->touch_effects = list_remove(view->touch_effects, fx);
            intro_effect_destroy(fx);
        }
    }

    nemoshow_dispatch_frame(view->show);
    nemotimer_set_timeout(timer, 5200 + delay);
}

static void intro_view_add_effect(IntroView *view, double ex, double ey)
{
    IntroEffect *fx = malloc(sizeof(IntroEffect));
    fx->time = time(NULL);
    view->touch_effects = list_append(view->touch_effects, fx);
    fx->ones = NULL;

    struct showone *one;
    one = path_draw(view->path0, view->group);
    nemoshow_item_set_anchor(one, 0.5, 0.5);
    nemoshow_item_translate(one, ex, ey);
    nemoshow_item_scale(one, 0.0, 0.0);
    nemoshow_item_set_alpha(one, 1.0);
    fx->ones = list_append(fx->ones, one);

    List *l;
    Path *path;
    LIST_FOR_EACH(view->paths, l, path) {
        one = path_draw(path, view->group);
        nemoshow_item_set_anchor(one, 0.5, 0.5);
        nemoshow_item_translate(one, ex, ey);
        nemoshow_item_scale(one, 0.0, 0.0);
        nemoshow_item_set_alpha(one, 1.0);
        fx->ones = list_append(fx->ones, one);
    }

    one = path_draw(view->path1, view->group);
    nemoshow_item_set_anchor(one, 0.5, 0.5);
    nemoshow_item_translate(one, ex, ey);
    nemoshow_item_scale(one, 0.0, 0.0);
    nemoshow_item_set_alpha(one, 1.0);
    fx->ones = list_append(fx->ones, one);

    int delay = 0;
    LIST_FOR_EACH_REVERSE(fx->ones, l, one) {
        _nemoshow_item_motion(one, NEMOEASE_CUBIC_OUT_TYPE, 3000, delay,
                "sx", 0.5, "sy", 0.5,
                "alpha", 0.0,
                "ro", 120.0,
                NULL);
        delay += 20;
    }
}

static void _intro_view_logo_grab_event(NemoWidgetGrab *grab, NemoWidget *widget, struct showevent *event, void *userdata)
{
    struct nemoshow *show = nemowidget_get_show(widget);
    double ex, ey;
    nemowidget_transform_from_global(widget,
            nemoshow_event_get_x(event),
            nemoshow_event_get_y(event), &ex, &ey);

    struct showone *one = userdata;
    IntroView *view = nemoshow_one_get_userdata(one);
    if (nemoshow_event_is_down(show, event)) {
        _nemoshow_item_motion_bounce(view->logo, NEMOEASE_CUBIC_INOUT_TYPE, 500, 0,
                "sx", 0.5, 0.75, "sy", 0.5, 0.75,
                NULL);
    } else if (nemoshow_event_is_up(show, event)) {
        _nemoshow_item_motion_bounce(view->logo, NEMOEASE_CUBIC_INOUT_TYPE, 500, 0,
                "sx", 1.25, 1.0, "sy", 1.25, 1.0,
                NULL);
        view->logo_grab = NULL;

        Karim *karim = view->karim;
        karim->type = KARIM_TYPE_MENU;
        intro_view_hide(karim->intro, NEMOEASE_CUBIC_IN_TYPE, 2000, 0);
        menu_view_show(karim->menu, NEMOEASE_CUBIC_OUT_TYPE, 1000, 1000);
    }
}

static void _intro_view_grab_event(NemoWidgetGrab *grab, NemoWidget *widget, struct showevent *event, void *userdata)
{
    struct nemoshow *show = nemowidget_get_show(widget);
    double ex, ey;
    nemowidget_transform_from_global(widget,
            nemoshow_event_get_x(event),
            nemoshow_event_get_y(event), &ex, &ey);

    IntroView *view = userdata;
    if (nemoshow_event_is_down(show, event)) {
        intro_view_add_effect(view, ex, ey);
        IntroGrab *ig = malloc(sizeof(IntroGrab));
        ig->cnt = 10;
        ig->device = nemoshow_event_get_device(event);
        view->grabs = list_append(view->grabs, ig);
    } else if (nemoshow_event_is_motion(show, event)) {
        List *l;
        IntroGrab *ig;
        LIST_FOR_EACH(view->grabs, l,  ig) {
            if (ig->device == nemoshow_event_get_device(event)) {
                ig->cnt--;
                if (ig->cnt == 0) {
                    intro_view_add_effect(view, ex, ey);
                    ig->cnt = 10;
                }
                break;
            }
        }
    } else if (nemoshow_event_is_up(show, event)) {
        List *l, *ll;
        IntroGrab *ig;
        LIST_FOR_EACH_SAFE(view->grabs, l,  ll, ig) {
            if (ig->device == nemoshow_event_get_device(event)) {
                view->grabs = list_remove(view->grabs, ig);
                free(ig);
            }
        }
    }
}

static void _intro_view_event(NemoWidget *widget, const char *id, void *info, void *userdata)
{
    struct showevent *event = info;
    IntroView *view = userdata;
    struct nemoshow *show = nemowidget_get_show(widget);

    double ex, ey;
    nemowidget_transform_from_global(widget,
            nemoshow_event_get_x(event),
            nemoshow_event_get_y(event), &ex, &ey);

    if (nemoshow_event_is_down(show, event)) {
        struct showone *one;
        one = nemowidget_pick_one(view->widget, ex, ey);
        if (one && !view->logo_grab) {
            view->logo_grab = nemowidget_create_grab(widget, event,
                    _intro_view_logo_grab_event, one);
        } else {
            nemowidget_create_grab(widget, event,
                    _intro_view_grab_event, view);
        }
    }
}

static IntroView *intro_view_create(Karim *karim, NemoWidget *parent, int width, int height)
{
    IntroView *view = calloc(sizeof(IntroView), 1);
    view->karim = karim;
    view->tool = nemowidget_get_tool(parent);
    view->show = nemowidget_get_show(parent);
    view->w = width;
    view->h = height;

    const char *uri;
    uri = KARIM_ICON_DIR"/intro/intro-motion1.svg";
    view->path0 = svg_get_path(uri);
    uri = KARIM_ICON_DIR"/intro/intro-motion2.svg";
    view->path1 = svg_get_path(uri);
    view->paths = path_get_median(view->path0, view->path1, 10);

    double sx, sy;
    sx = (double)width/view->path0->width;
    sy = (double)height/view->path0->height;
    if (sx > sy) view->scale = sx * 2.25;
    else view->scale = sy * 2.25;

    NemoWidget *widget;
    view->widget = widget = nemowidget_create_vector(parent, width, height);
    nemowidget_append_callback(widget, "event", _intro_view_event, view);
    nemowidget_set_alpha(view->widget, 0, 0, 0, 0.0);

    struct showone *group;
    struct showone *one;
    view->group = group = GROUP_CREATE(nemowidget_get_canvas(widget));

    view->bg = one = RECT_CREATE(group, width, height);
    nemoshow_item_set_fill_color(one, RGBA(WHITE));

    double w, h;

    uri = KARIM_ICON_DIR"/intro/intro-karimlogo.svg";
    svg_get_wh(uri, &w, &h);
    view->logo = one = SVG_PATH_GROUP_CREATE(group, w, h, uri);
    nemoshow_one_set_state(one, NEMOSHOW_PICK_STATE);
    nemoshow_one_set_userdata(one, view);
    nemoshow_item_set_anchor(one, 0.5, 0.5);
    nemoshow_item_translate(one, width/2, height/2);
    nemoshow_item_scale(one, 0.0, 0.0);
    nemoshow_item_set_alpha(one, 0.0);

    int i;
    int cnt = 4;
    for (i = 0 ; i < cnt ; i++) {
        IntroEffect *fx = malloc(sizeof(IntroEffect));
        fx->view = view;
        view->effects = list_append(view->effects, fx);
        fx->ones = NULL;

        one = path_draw(view->path0, group);
        nemoshow_item_set_anchor(one, 0.5, 0.5);
        nemoshow_item_translate(one, width/2, height/2);
        nemoshow_item_scale(one, 0.0, 0.0);
        nemoshow_item_set_alpha(one, 0.0);
        fx->ones = list_append(fx->ones, one);

        List *l;
        Path *path;
        LIST_FOR_EACH(view->paths, l, path) {
            one = path_draw(path, group);
            nemoshow_item_set_anchor(one, 0.5, 0.5);
            nemoshow_item_translate(one, width/2, height/2);
            nemoshow_item_scale(one, 0.0, 0.0);
            nemoshow_item_set_alpha(one, 0.0);
            fx->ones = list_append(fx->ones, one);
        }

        one = path_draw(view->path1, group);
        nemoshow_item_set_anchor(one, 0.5, 0.5);
        nemoshow_item_translate(one, width/2, height/2);
        nemoshow_item_scale(one, 0.0, 0.0);
        nemoshow_item_set_alpha(one, 0.0);
        fx->ones = list_append(fx->ones, one);
    }

    view->timer = TOOL_ADD_TIMER(view->tool, 0, _intro_view_timeout, view);

    return view;
}

typedef struct _SaverWave SaverWave;
struct _SaverWave {
    SaverView *view;
    struct showone *group;
    Path *path0, *path1;
    List *ones;
    struct nemotimer *timer;
    int idx;
};

struct _SaverView {
    Karim *karim;
    int w, h;
    struct nemotool *tool;
    struct nemoshow *show;
    NemoWidget *parent;

    NemoWidget *bg_widget;
    Image *bg;

    NemoWidget *widget;
    struct showone *group;
    struct showone *logo;
    List *waves;
};

static void _saver_wave_timeout(struct nemotimer *timer, void *userdata)
{
    SaverWave *wave = userdata;

    int cnt = list_count(wave->ones);

    int duration = 10000;
    uint32_t easetype = NEMOEASE_SINUSOIDAL_IN_TYPE;

    struct showone *one;
    one = LIST_DATA(list_get_nth(wave->ones, wave->idx));
    nemoshow_item_set_alpha(one, 0.0);
    _nemoshow_item_motion(one, easetype, 500, 0,
            "alpha", 1.0, NULL);
    path_array_morph(one, 0, 0, 0, wave->path0);
    path_array_morph(one, easetype, duration, 0, wave->path1);

    wave->idx++;
    if (wave->idx >= cnt) wave->idx = 0;

    nemotimer_set_timeout(wave->timer, duration/cnt);
    nemoshow_dispatch_frame(wave->view->show);
}

SaverWave *saver_view_create_wave(SaverView *view, const char *uri0, const char *uri1,
        double alpha0, double alpha1, int cnt)
{
    SaverWave *wave = calloc(sizeof(SaverWave), 1);
    wave->view = view;

    struct showone *one;
    struct showone *group;
    wave->group = group = GROUP_CREATE(view->group);

    wave->path0 = svg_get_path(uri0);
    wave->path1 = svg_get_path(uri1);

    uint32_t mask0 = 0xFFFFFF00 + 255 * alpha0;
    uint32_t mask1 = 0xFFFFFF00 + 255 * 0.0;
    wave->path0->fill &= mask0;
    wave->path0->stroke &= mask0;
    wave->path1->fill &= mask1;
    wave->path1->stroke &= mask1;

    double sx, sy;
    sx = view->w/3840.0;
    sy = view->h/2160.0;

    double w, h;
    const char *uri;
    uri = KARIM_ICON_DIR"/saver/BG-logo.svg";
    svg_get_wh(uri, &w, &h);
    w = w * sx;
    h = h * sy;
    view->logo = one = SVG_PATH_GROUP_CREATE(group, w, h, uri);
    nemoshow_item_translate(one, view->w/2, view->h/2);
    nemoshow_item_set_anchor(one, 0.5, 0.5);

    int i;
    for (i = 0 ; i < cnt ; i++) {
        one = path_draw_array(wave->path0, group);
        nemoshow_item_scale(one, sx, sy);
        nemoshow_item_set_alpha(one, 0.0);
        wave->ones = list_append(wave->ones, one);
    }

    wave->timer = TOOL_ADD_TIMER(view->tool, 0, _saver_wave_timeout, wave);

    return wave;
}

static void saver_wave_show(SaverWave *wave, uint32_t easetype, int duration, int delay)
{
    wave->idx = 0;
    nemotimer_set_timeout(wave->timer, 10 + delay);
    if (duration > 0) {
        _nemoshow_item_motion(wave->group, easetype, duration, delay,
                "alpha", 1.0,
                NULL);
    } else {
        nemoshow_item_set_alpha(wave->group, 1.0);
    }
}

static void saver_wave_hide(SaverWave *wave, uint32_t easetype, int duration, int delay)
{
    nemotimer_set_timeout(wave->timer, 0);
    List *l;
    struct showone *one;
    LIST_FOR_EACH(wave->ones, l, one) {
        path_array_morph(one, easetype, duration, delay, wave->path0);
    }
    if (duration > 0) {
        _nemoshow_item_motion(wave->group, easetype, duration, delay,
                "alpha", 0.0,
                NULL);
    } else {
        nemoshow_item_set_alpha(wave->group, 0.0);
    }
}

void saver_view_show(SaverView *view, uint32_t easetype, int duration, int delay)
{
    nemowidget_show(view->bg_widget, 0, 0, 0);
    nemowidget_set_alpha(view->bg_widget, easetype, duration, delay, 1.0);
    nemowidget_show(view->widget, 0, 0, 0);
    nemowidget_set_alpha(view->widget, easetype, duration, delay, 1.0);
    nemowidget_scale(view->widget, easetype, duration, delay, 1.0, 1.0);
    nemowidget_translate(view->widget, easetype, duration, delay, 0, 0);

    List *l;
    SaverWave *wave;
    int _delay = 0;
    int i = 1;
    LIST_FOR_EACH(view->waves, l, wave) {
        saver_wave_show(wave, easetype, duration, delay + _delay);
        _delay += 250;
        i++;
    }
    nemoshow_dispatch_frame(view->show);
}

void saver_view_hide(SaverView *view, uint32_t easetype, int duration, int delay)
{
    nemowidget_hide(view->bg_widget, 0, 0, 0);
    nemowidget_set_alpha(view->bg_widget, easetype, duration, delay, 0.0);
    nemowidget_hide(view->widget, 0, 0, 0);
    nemowidget_set_alpha(view->widget, easetype, duration, delay, 0.0);
    nemowidget_scale(view->widget, easetype, duration, delay, 0.0, 0.0);
    nemowidget_translate(view->widget, easetype, duration, delay, view->w/2, view->h/2);

    List *l;
    SaverWave *wave;
    LIST_FOR_EACH(view->waves, l, wave) {
        saver_wave_hide(wave, easetype, duration, delay);
    }
    nemoshow_dispatch_frame(view->show);
}

static void _saver_view_event(NemoWidget *widget, const char *id, void *info, void *userdata)
{
    struct showevent *event = info;
    SaverView *view = userdata;
    Karim *karim = view->karim;
    struct nemoshow *show = nemowidget_get_show(widget);

    if (nemoshow_event_is_down(show, event)) {
        nemotimer_set_timeout(karim->saver_timer, SAVER_TIMEOUT);

        saver_view_hide(view, NEMOEASE_CUBIC_IN_TYPE, 3000, 0);
        if (karim->type == KARIM_TYPE_NONE) {
            intro_view_show(karim->intro, NEMOEASE_CUBIC_OUT_TYPE, 1000, 500);
        } else if (karim->type == KARIM_TYPE_INTRO) {
            intro_view_show(karim->intro, NEMOEASE_CUBIC_OUT_TYPE, 1000, 500);
        } else if (karim->type == KARIM_TYPE_HONEY) {
            honey_view_show(karim->honey, NEMOEASE_CUBIC_OUT_TYPE, 1000, 500);
        } else if (karim->type == KARIM_TYPE_VIEWER) {
            viewer_view_show(karim->viewer, NEMOEASE_CUBIC_OUT_TYPE, 1000, 500);
        } else {
            menu_view_show(karim->menu, NEMOEASE_CUBIC_OUT_TYPE, 1000, 500);
            if (karim->type == KARIM_TYPE_REGION) {
                region_view_show(karim->region, NEMOEASE_CUBIC_OUT_TYPE, 1000, 500);
            } else if (karim->type == KARIM_TYPE_WORK) {
                work_view_show(karim->work, NEMOEASE_CUBIC_OUT_TYPE, 1000, 500);
            } else if (karim->type == KARIM_TYPE_YEAR) {
                year_view_show(karim->year, NEMOEASE_CUBIC_OUT_TYPE, 1000, 500);
            }
        }
    }
}

SaverView *saver_view_create(Karim *karim, NemoWidget *parent, int width, int height)
{
    SaverView *view = calloc(sizeof(SaverView), 1);
    view->karim = karim;
    view->tool = nemowidget_get_tool(parent);
    view->show = nemowidget_get_show(parent);
    view->w = width;
    view->h = height;

    // Designed for 3840x2160
    double sx= view->w/3840.0;
    double sy = view->h/2160.0;

    NemoWidget *widget;
    view->bg_widget = widget = nemowidget_create_vector(parent, width, height);
    nemowidget_enable_event_repeat(widget, true);
    nemowidget_set_alpha(widget, 0, 0, 0, 0.0);

    int w, h;
    const char *uri = KARIM_IMG_DIR"/saver/BG-base.png";
    file_get_image_wh(uri, &w, &h);
    w *= sx;
    h *= sy;

    Image *img;
    view->bg = img = image_create(nemowidget_get_canvas(widget));
    image_load_full(img, view->tool, uri, w, h, NULL, NULL);

    view->widget = widget = nemowidget_create_vector(parent, width, height);
    nemowidget_append_callback(widget, "event", _saver_view_event, view);
    nemowidget_set_alpha(widget, 0, 0, 0, 0.0);
    nemowidget_scale(view->widget, 0, 0, 0, 0.0, 0.0);
    nemowidget_translate(view->widget, 0, 0, 0, view->w/2, view->h/2);

    struct showone *group;
    view->group = group = GROUP_CREATE(nemowidget_get_canvas(widget));

    int i;
    for (i = 1 ; i <= 5; i++) {
        char buf0[PATH_MAX], buf1[PATH_MAX];
        snprintf(buf0, PATH_MAX, KARIM_ICON_DIR"/saver/BG-%d-1.svg", i);
        snprintf(buf1, PATH_MAX, KARIM_ICON_DIR"/saver/BG-%d-2.svg", i);
        double a0, a1;
        if (i == 1) {
            a0 = 1.0;
            a1 = 0.5;
        } else if (i == 2 || i == 3) {
            a0 = 0.5;
            a1 = 0.3;
        } else if (i == 4 || i == 5) {
            a0 = 0.8;
            a1 = 0.1;
        }

        int cnt = 10;
        if (i == 4 || i == 5) cnt = 13;
        if (i == 2 || i == 3) cnt = 20;
        SaverWave *wave;
        wave = saver_view_create_wave(view, buf0, buf1, a0, a1, cnt);
        view->waves = list_append(view->waves, wave);
    }

    return view;
}

static void _karim_event(NemoWidget *widget, const char *id, void *info, void *userdata)
{
    struct showevent *event = info;
    Karim *karim = userdata;
    struct nemoshow *show = nemowidget_get_show(widget);
    nemotimer_set_timeout(karim->saver_timer, SAVER_TIMEOUT);

    if (nemoshow_event_is_keyboard_up(show, event)) {
        uint32_t keycode = nemoshow_event_get_value(event);

        if (karim->type == KARIM_TYPE_NONE) {
            karim->type = KARIM_TYPE_INTRO;
            saver_view_hide(karim->saver, NEMOEASE_CUBIC_IN_TYPE, 3000, 0);
            intro_view_show(karim->intro, NEMOEASE_CUBIC_OUT_TYPE, 1000, 500);
        } else if (karim->type == KARIM_TYPE_INTRO) {
            karim->type = KARIM_TYPE_MENU;
            _nemoshow_item_motion_bounce(karim->intro->logo, NEMOEASE_CUBIC_INOUT_TYPE, 500, 0,
                    "sx", 1.25, 1.0, "sy", 1.25, 1.0,
                    NULL);
            intro_view_hide(karim->intro, NEMOEASE_CUBIC_IN_TYPE, 2000, 0);
            menu_view_show(karim->menu, NEMOEASE_CUBIC_OUT_TYPE, 1000, 1000);
        } else if (karim->type == KARIM_TYPE_VIEWER && karim->viewer->mode == VIEWER_MODE_GALLERY) {
            ViewerView *view = karim->viewer;
            if (keycode == 105) { // left
                view->gallery_x += view->w;
            } else if (keycode == 106)  { // right
                view->gallery_x -= view->w;
            }

            int cnt = list_count(view->items);
            if (view->gallery_x > 0) view->gallery_x = 0;
            else if (view->gallery_x < (double)-(cnt - 1) * view->w)
                view->gallery_x = -(cnt - 1) * view->w;

            int i = 0;
            List *l;
            ViewerItem *item;
            LIST_FOR_EACH(view->items, l, item) {
                viewer_item_translate(item, NEMOEASE_CUBIC_INOUT_TYPE, 500, 0,
                        view->gallery_x + view->w * i, 0);
                i++;
            }
        } else {
            uint32_t color0 = 0xE70095FF;
            uint32_t color1 = 0xF7ACB87F;
            MenuView *view = karim->menu;
            if (keycode == 105) { // left
                if (karim->type == KARIM_TYPE_MENU) {
                    year_view_show(karim->year, NEMOEASE_CUBIC_OUT_TYPE, 1000, 500);
                    karim->type = KARIM_TYPE_YEAR;
                    _nemoshow_item_motion_bounce(view->btn_year, NEMOEASE_CUBIC_INOUT_TYPE, 500, 0,
                            "sx", 0.8, 1.0, "sy", 0.8, 1.0,
                            NULL);
                    _nemoshow_item_motion(view->btn_region, NEMOEASE_CUBIC_INOUT_TYPE, 500, 0,
                            "fill", color1,
                            NULL);
                    _nemoshow_item_motion(view->btn_wave, NEMOEASE_CUBIC_INOUT_TYPE, 500, 0,
                            "fill", color1,
                            NULL);
                    _nemoshow_item_motion(view->btn_year, NEMOEASE_CUBIC_INOUT_TYPE, 500, 0,
                            "fill", color0,
                            NULL);
                } else if (karim->type == KARIM_TYPE_WORK) {
                    work_view_hide(karim->work, NEMOEASE_CUBIC_IN_TYPE, 1000, 0);
                    region_view_show(karim->region, NEMOEASE_CUBIC_OUT_TYPE, 1000, 500);
                    karim->type = KARIM_TYPE_REGION;
                    _nemoshow_item_motion_bounce(view->btn_region, NEMOEASE_CUBIC_INOUT_TYPE, 500, 0,
                            "sx", 0.8, 1.0, "sy", 0.8, 1.0,
                            NULL);
                    _nemoshow_item_motion(view->btn_region, NEMOEASE_CUBIC_INOUT_TYPE, 500, 0,
                            "fill", color0,
                            NULL);
                    _nemoshow_item_motion(view->btn_wave, NEMOEASE_CUBIC_INOUT_TYPE, 500, 0,
                            "fill", color1,
                            NULL);
                    _nemoshow_item_motion(view->btn_year, NEMOEASE_CUBIC_INOUT_TYPE, 500, 0,
                            "fill", color1,
                            NULL);
                } else if (karim->type == KARIM_TYPE_YEAR) {
                    year_view_hide(karim->year, NEMOEASE_CUBIC_IN_TYPE, 1000, 0);
                    work_view_show(karim->work, NEMOEASE_CUBIC_OUT_TYPE, 1000, 500);
                    karim->type = KARIM_TYPE_WORK;
                    _nemoshow_item_motion_bounce(view->btn_wave, NEMOEASE_CUBIC_INOUT_TYPE, 500, 0,
                            "sx", 0.8, 1.0, "sy", 0.8, 1.0,
                            NULL);
                    _nemoshow_item_motion(view->btn_region, NEMOEASE_CUBIC_INOUT_TYPE, 500, 0,
                            "fill", color1,
                            NULL);
                    _nemoshow_item_motion(view->btn_wave, NEMOEASE_CUBIC_INOUT_TYPE, 500, 0,
                            "fill", color0,
                            NULL);
                    _nemoshow_item_motion(view->btn_year, NEMOEASE_CUBIC_INOUT_TYPE, 500, 0,
                            "fill", color1,
                            NULL);
                }
            } else if (keycode == 106) { // right
                if (karim->type == KARIM_TYPE_MENU) {
                    region_view_show(karim->region, NEMOEASE_CUBIC_OUT_TYPE, 1000, 500);
                    karim->type = KARIM_TYPE_REGION;
                    _nemoshow_item_motion_bounce(view->btn_region, NEMOEASE_CUBIC_INOUT_TYPE, 500, 0,
                            "sx", 0.8, 1.0, "sy", 0.8, 1.0,
                            NULL);
                    _nemoshow_item_motion(view->btn_region, NEMOEASE_CUBIC_INOUT_TYPE, 500, 0,
                            "fill", color0,
                            NULL);
                    _nemoshow_item_motion(view->btn_wave, NEMOEASE_CUBIC_INOUT_TYPE, 500, 0,
                            "fill", color1,
                            NULL);
                    _nemoshow_item_motion(view->btn_year, NEMOEASE_CUBIC_INOUT_TYPE, 500, 0,
                            "fill", color1,
                            NULL);
                } else if (karim->type == KARIM_TYPE_REGION) {
                    region_view_hide(karim->region, NEMOEASE_CUBIC_IN_TYPE, 1000, 0);
                    work_view_show(karim->work, NEMOEASE_CUBIC_OUT_TYPE, 1000, 500);
                    karim->type = KARIM_TYPE_WORK;
                    _nemoshow_item_motion_bounce(view->btn_wave, NEMOEASE_CUBIC_INOUT_TYPE, 500, 0,
                            "sx", 0.8, 1.0, "sy", 0.8, 1.0,
                            NULL);
                    _nemoshow_item_motion(view->btn_region, NEMOEASE_CUBIC_INOUT_TYPE, 500, 0,
                            "fill", color1,
                            NULL);
                    _nemoshow_item_motion(view->btn_wave, NEMOEASE_CUBIC_INOUT_TYPE, 500, 0,
                            "fill", color0,
                            NULL);
                    _nemoshow_item_motion(view->btn_year, NEMOEASE_CUBIC_INOUT_TYPE, 500, 0,
                            "fill", color1,
                            NULL);
                } else if (karim->type == KARIM_TYPE_WORK) {
                    work_view_hide(karim->work, NEMOEASE_CUBIC_IN_TYPE, 1000, 0);
                    year_view_show(karim->year, NEMOEASE_CUBIC_OUT_TYPE, 1000, 500);
                    karim->type = KARIM_TYPE_YEAR;
                    _nemoshow_item_motion_bounce(view->btn_year, NEMOEASE_CUBIC_INOUT_TYPE, 500, 0,
                            "sx", 0.8, 1.0, "sy", 0.8, 1.0,
                            NULL);
                    _nemoshow_item_motion(view->btn_region, NEMOEASE_CUBIC_INOUT_TYPE, 500, 0,
                            "fill", color1,
                            NULL);
                    _nemoshow_item_motion(view->btn_wave, NEMOEASE_CUBIC_INOUT_TYPE, 500, 0,
                            "fill", color1,
                            NULL);
                    _nemoshow_item_motion(view->btn_year, NEMOEASE_CUBIC_INOUT_TYPE, 500, 0,
                            "fill", color0,
                            NULL);
                }
            } else {
                if (karim->type == KARIM_TYPE_WORK) {
                    WorkView *view = karim->work;
                    WorkIcon *icon = NULL, *_icon  = NULL;
                    if (keycode == 2) {
                        icon = LIST_DATA(list_get_nth(view->icons, 12));
                    } else if (keycode == 3) {
                        icon = LIST_DATA(list_get_nth(view->icons, 11));
                        _icon = LIST_DATA(list_get_nth(view->icons, 12));
                    } else if (keycode == 4) {
                        icon = LIST_DATA(list_get_nth(view->icons, 10));
                        _icon = LIST_DATA(list_get_nth(view->icons, 11));
                    } else if (keycode == 5) {
                        icon = LIST_DATA(list_get_nth(view->icons, 7));
                        _icon = LIST_DATA(list_get_nth(view->icons, 10));
                    } else if (keycode == 6) {
                        icon = LIST_DATA(list_get_nth(view->icons, 0));
                        _icon = LIST_DATA(list_get_nth(view->icons, 7));
                    }
                    if (icon)
                        work_icon_down(icon, NEMOEASE_CUBIC_INOUT_TYPE, 1000, 0);
                    if (_icon)
                        work_icon_up(_icon, NEMOEASE_CUBIC_INOUT_TYPE, 1000, 0);
                }
            }
        }
        nemoshow_dispatch_frame(karim->show);
    }
}

static void _karim_saver_timeout(struct nemotimer *timer, void *userdata)
{
    Karim *karim = userdata;
    saver_view_show(karim->saver, NEMOEASE_CUBIC_OUT_TYPE, 2000, 0);

    menu_view_hide(karim->menu, NEMOEASE_CUBIC_IN_TYPE, 1000, 0);
    if (karim->type == KARIM_TYPE_INTRO) {
        intro_view_hide(karim->intro, NEMOEASE_CUBIC_IN_TYPE, 2000, 0);
    } else if (karim->type == KARIM_TYPE_REGION) {
        region_view_hide(karim->region, NEMOEASE_CUBIC_IN_TYPE, 1000, 0);
    } else if (karim->type == KARIM_TYPE_WORK) {
        work_view_hide(karim->work, NEMOEASE_CUBIC_IN_TYPE, 1000, 0);
    } else if (karim->type == KARIM_TYPE_YEAR) {
        year_view_hide(karim->year, NEMOEASE_CUBIC_IN_TYPE, 1000, 0);
    } else if (karim->type == KARIM_TYPE_HONEY) {
        honey_view_hide(karim->honey, NEMOEASE_CUBIC_IN_TYPE, 1000, 0);
    } else if (karim->type == KARIM_TYPE_VIEWER) {
        viewer_view_hide(karim->viewer, NEMOEASE_CUBIC_IN_TYPE, 1000, 0);
    }
}

static Karim *karim_create(NemoWidget *parent, int width, int height)
{
    Karim *karim = calloc(sizeof(Karim), 1);
    karim->parent = parent;
    karim->w = width;
    karim->h = height;
    karim->tool = nemowidget_get_tool(parent);
    karim->show = nemowidget_get_show(parent);
    karim->uuid = nemowidget_get_uuid(parent);

    NemoWidget *widget;
    karim->widget = widget = nemowidget_create_vector(parent, width, height);
    nemowidget_set_alpha(widget, 0, 0, 0, 0.0);

    struct showone *one;
    karim->bg = one = RECT_CREATE(nemowidget_get_canvas(widget), width, height);
    nemoshow_item_set_fill_color(one, RGBA(WHITE));
    nemoshow_item_set_alpha(one, 1.0);

    karim->intro = intro_view_create(karim, parent, width, height);
    karim->region = region_view_create(karim, parent, width, height);
    karim->work = work_view_create(karim, parent, width, height);
    karim->year = year_view_create(karim, parent, width, height);
    karim->menu = menu_view_create(karim, parent, width, height);
    karim->viewer = viewer_view_create(karim, karim->parent, karim->w, karim->h);
    karim->saver_timer = TOOL_ADD_TIMER(karim->tool, 0, _karim_saver_timeout, karim);

    karim->event_widget = widget = nemowidget_create_vector(parent, width, height);
    nemowidget_append_callback(widget, "event", _karim_event, karim);
    nemowidget_enable_event_repeat(widget, true);

    karim->saver = saver_view_create(karim, parent, width, height);

    return karim;
}

static void karim_destroy(Karim *karim)
{
    intro_view_destroy(karim->intro);
    free(karim);
}

static void karim_show(Karim *karim, uint32_t easetype, int duration, int delay)
{
    nemotimer_set_timeout(karim->saver_timer, SAVER_TIMEOUT);
    nemowidget_show(karim->widget, 0, 0, 0);
    nemowidget_set_alpha(karim->widget, easetype, duration, delay, 1.0);
    nemowidget_show(karim->event_widget, 0, 0, 0);
    saver_view_show(karim->saver, easetype, duration, delay);
    karim->type = KARIM_TYPE_NONE;
}

static void karim_hide(Karim *karim, uint32_t easetype, int duration, int delay)
{
    nemotimer_set_timeout(karim->saver_timer, 0);
    nemowidget_hide(karim->widget, 0, 0, 0);
    nemowidget_set_alpha(karim->widget, easetype, duration, delay, 0.0);
    nemowidget_hide(karim->event_widget, 0, 0, 0);

    saver_view_hide(karim->saver, easetype, duration, delay);
    intro_view_hide(karim->intro, easetype, duration, delay);
    menu_view_hide(karim->menu, easetype, duration, delay);
    region_view_hide(karim->region, easetype, duration, delay);
    work_view_hide(karim->work, easetype, duration, delay);
    year_view_hide(karim->year, easetype, duration, delay);
    honey_view_hide(karim->honey, easetype, duration, delay);
    viewer_view_hide(karim->viewer, easetype, duration, delay);
}

static void _win_exit(NemoWidget *win, const char *id, void *info, void *userdata)
{
    Karim *karim = userdata;
    _nemoshow_destroy_transition_all(karim->show);

    karim_hide(karim, NEMOEASE_CUBIC_IN_TYPE, 500, 0);

    nemowidget_win_exit_if_no_trans(win);
}

int main(int argc, char *argv[])
{
    Config *config;
    config = config_load(PROJECT_NAME, APPNAME, CONFXML, argc, argv);

    struct nemotool *tool = TOOL_CREATE();
    NemoWidget *win = nemowidget_create_win_base(tool, APPNAME, config);
    nemowidget_win_set_anchor(win, 0.0, 0.0);
    nemowidget_win_set_layer(win, "background");
    nemowidget_win_enable_move(win, 0);
    nemowidget_win_enable_rotate(win, 0);
    nemowidget_win_enable_scale(win, 0);

    Karim *karim;
    karim = karim_create(win, config->width, config->height);
    nemowidget_append_callback(win, "exit", _win_exit, karim);
    karim_show(karim, NEMOEASE_CUBIC_OUT_TYPE, 2000, 0);

    nemowidget_show(win, 0, 0, 0);
    nemotool_run(tool);

    karim_destroy(karim);
    nemowidget_destroy(win);
    TOOL_DESTROY(tool);
    config_unload(config);

    return 0;
}
