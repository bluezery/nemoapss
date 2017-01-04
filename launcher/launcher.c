#include <unistd.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <getopt.h>
#include <libgen.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <nemotool.h>
#include <nemotimer.h>
#include <nemoshow.h>
#include <nemoplay.h>

#include "nemoutil.h"
#include "widgets.h"
#include "nemoui.h"
#include "sound.h"

#define FPS_TIMEOUT 1000/60

typedef struct _ConfigApp ConfigApp;
struct _ConfigApp {
    char *type;
    Config *config;
    double sxy;
};

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

    double sx = 1.0;
    double sy = 1.0;
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
    if (width > 0) sx = (double)app->config->width/width;
    if (height > 0) sy = (double)app->config->height/height;
    if (sx > sy) app->sxy = sy;
    else app->sxy = sx;

    xml_unload(xml);


    struct option options[] = {
        {"type", required_argument, NULL, 't'},
        { NULL }
    };
    int opt;
    while ((opt = getopt_long(argc, argv, "t:", options, NULL)) != -1) {
        switch(opt) {
            case 't':
                app->type = strdup(optarg);
                break;
            default:
                break;
        }
    }
    if (!app->type) app->type = strdup("table");

    return app;
}

static void _config_unload(ConfigApp *app)
{
    config_unload(app->config);
    free(app->type);
    free(app);
}

typedef struct _MovieBox MovieBox;
struct _MovieBox {
    int width, height;
    char *path;
    struct playbox *box;
    int cnt;
    int pixel_format;
};

List *__movie_boxes = NULL;

// FIXME: consider asynchronous later
MovieBox *movie_box_create(const char *path)
{
    RET_IF(!file_is_exist(path), NULL);

    MovieBox *mbox = calloc(sizeof(MovieBox), 1);
    mbox->path = strdup(path);

    // XXX: caches
    List *l;
    MovieBox *_mbox;
    LIST_FOR_EACH(__movie_boxes, l, _mbox) {
        if (!strcmp(path, _mbox->path)) {
            break;
        }
    }
    if (_mbox) {
        mbox->box = _mbox->box;
        mbox->pixel_format = _mbox->pixel_format;
        mbox->width = _mbox->width;
        mbox->height = _mbox->height;
    } else {
        struct nemoplay *play;
        play = nemoplay_create();
        nemoplay_load_media(play, path);

        struct playbox *box;
        mbox->box = box = nemoplay_box_create(nemoplay_get_video_framecount(play));
        if (!box) {
            ERR("No box!!: %s", path);
            nemoplay_destroy(play);
            free(mbox);
            return NULL;
        }
        nemoplay_extract_video(play, box, nemoplay_get_video_framecount(play));

        mbox->cnt = nemoplay_box_get_count(box);

        mbox->pixel_format = nemoplay_get_pixel_format(play);
        mbox->width = nemoplay_get_video_width(play);
        mbox->height = nemoplay_get_video_height(play);
        ERR("%d %d (%s) %d", mbox->width, mbox->height, path, mbox->cnt);
        nemoplay_destroy(play);
    }

    return mbox;
}

void movie_box_destroy(MovieBox *mbox)
{
    nemoplay_box_destroy(mbox->box);
    free(mbox->path);
    free(mbox);
}

typedef struct _Movie Movie;
struct _Movie {
    int width, height;
    NemoWidget *widget;
    struct playshader *shader;

    MovieBox *mbox;
    bool repeat;
    int idx;
    struct showtransition *trans;
};

static void _movie_resize(NemoWidget *widget, const char *id, void *info, void *userdata)
{
    NemoWidgetInfo_Resize *resize = info;
    Movie *movie = userdata;

    nemoplay_shader_set_viewport(movie->shader,
            nemowidget_get_texture(movie->widget),
            resize->width, resize->height);

    if (movie->mbox) {
        struct playone *one;
        ERR("%d", movie->idx);
        one = nemoplay_box_get_one(movie->mbox->box, movie->idx);
        if (!one) {
            ERR("nemoplay_box_get_one failed: %d", movie->idx);
        } else {
            nemoplay_shader_update(movie->shader, one);
            nemoplay_shader_dispatch(movie->shader);
            nemowidget_dirty(movie->widget);
            struct nemoshow *show = nemowidget_get_show(widget);
            nemoshow_dispatch_frame(show);
        }
    }
}

static int _movie_frame(void *userdata, uint32_t time, double t)
{
    Movie *movie = userdata;
    struct playone *one;
	one = nemoplay_box_get_one(movie->mbox->box, movie->idx);
    if (!one) {
        ERR("nemoplay_box_get_one failed: %d", movie->idx);
        movie->trans = NULL;
        return 1;
    }

    //ERR("%s, %d, %lf, (%d)", movie->mbox->path, time, t, movie->idx);
    nemoplay_shader_update(movie->shader, one);
    nemoplay_shader_dispatch(movie->shader);
    nemowidget_dirty(movie->widget);
    struct nemoshow *show = nemowidget_get_show(movie->widget);
    nemoshow_dispatch_frame(show);

    if (movie->idx >= 0) {
        movie->idx++;
        if (movie->idx >= movie->mbox->cnt) {
            if (movie->repeat) {
                movie->idx = 0;
            } else {
                movie->trans = NULL;
                return 1;
            }
        }
    }

    return 0;
}

static int _movie_frame_rewind(void *userdata, uint32_t time, double t)
{
    Movie *movie = userdata;
    struct playone *one;
	one = nemoplay_box_get_one(movie->mbox->box, movie->idx);
    if (!one) {
        ERR("nemoplay_box_get_one failed: %d", movie->idx);
        movie->trans = NULL;
        return 1;
    }

    //ERR("%s, %d, %lf, (%d)", movie->mbox->path, time, t, movie->idx);
    nemoplay_shader_update(movie->shader, one);
    nemoplay_shader_dispatch(movie->shader);
    nemowidget_dirty(movie->widget);
    struct nemoshow *show = nemowidget_get_show(movie->widget);
    nemoshow_dispatch_frame(show);

    if (movie->idx > 0) {
        movie->idx--;
    } else {
        movie->trans = NULL;
        return 1;
    }

    return 0;
}

void movie_translate(Movie *movie, uint32_t easetype, int duration, int delay, int tx, int ty)
{
    nemowidget_translate(movie->widget, easetype, duration, delay, tx, ty);
}

Movie *movie_create(NemoWidget *parent, int width, int height)
{
    Movie *movie = calloc(sizeof(Movie), 1);
    movie->width = width;
    movie->height = height;

    movie->shader = nemoplay_shader_create();

    NemoWidget *widget;
    movie->widget = widget = nemowidget_create_opengl(parent, width, height);
    nemowidget_append_callback(widget, "resize", _movie_resize, movie);
    nemowidget_show(movie->widget, 0, 0, 0);

    return movie;
}

void movie_rewind(Movie *movie)
{
    movie->repeat = false;

    struct nemoshow *show = nemowidget_get_show(movie->widget);

    if (movie->trans) nemoshow_transition_destroy(movie->trans);

    struct showtransition *trans;
    movie->trans = trans = nemoshow_transition_create(NEMOEASE_LINEAR_TYPE, 0, 0);
    nemoshow_transition_set_dispatch_frame(trans, _movie_frame_rewind);
    nemoshow_transition_set_repeat(trans, 0);
    nemoshow_transition_set_userdata(trans, movie);
    nemoshow_attach_transition(show, trans);
}

void movie_play(Movie *movie, MovieBox *mbox, bool repeat)
{
    movie->mbox = mbox;
    movie->repeat = repeat;
    movie->idx = 0;

    struct nemoshow *show = nemowidget_get_show(movie->widget);

    struct showtransition *trans;
    movie->trans = trans = nemoshow_transition_create(NEMOEASE_LINEAR_TYPE, 0, 0);
    nemoshow_transition_set_dispatch_frame(trans, _movie_frame);
    nemoshow_transition_set_repeat(trans, 0);
    nemoshow_transition_set_userdata(trans, movie);
    nemoshow_attach_transition(show, trans);

    nemoplay_shader_set_format(movie->shader,
            mbox->pixel_format);
    nemoplay_shader_resize(movie->shader,
            mbox->width, mbox->height);
    ERR("%d %d %d", mbox->pixel_format, mbox->width, mbox->height);
}

typedef struct _AnimatorScene AnimatorScene;
struct _AnimatorScene {
    char *path;
    bool repeat;
    struct nemoplay *play;
    struct playbox *box;
};

void animator_scene_make_reverse(AnimatorScene *scene)
{
    RET_IF(!scene);
    struct playbox *box = scene->box;
    int i = 0;
    for (i = 0 ; i < box->nones ; i++) {
        if (i >= box->nones - i - 1) break;
        struct playone *temp;
        temp = box->ones[i];
        box->ones[i] = box->ones[box->nones - i - 1];
        box->ones[box->nones - i - 1] = temp;
    }
}

int animator_scene_get_frame_count(AnimatorScene *scene)
{
    RET_IF(!scene, -1);
    return  nemoplay_box_get_count(scene->box);
}

typedef struct _Animator Animator;
typedef void (*AnimatorDone)(Animator *anim, void *userdata);

struct _Animator {
    int width, height;
    struct nemoshow *show;
    struct nemotool *tool;

    struct playshader *shader;
    NemoWidget *widget;

    bool reverse;
    bool rewind;
    struct nemotimer *timer;
    int box_idx;
    List *scenes;
    AnimatorScene *scene;
    AnimatorScene *scene_next;
};

void animator_play(Animator *anim)
{
    /*
    nemotimer_set_timeout(anim->timer, 10);
    nemotimer_set_timeout(anim->timer_reverse, 0);
    nemotimer_set_timeout(anim->timer_rewind, 0);
    nemotimer_set_timeout(anim->timer_rewind_reverse, 0);
    */
    anim->reverse = false;
    anim->rewind = false;
    nemotimer_set_timeout(anim->timer, 10);
}

void animator_play_reverse(Animator *anim)
{
    /*
    nemotimer_set_timeout(anim->timer, 0);
    nemotimer_set_timeout(anim->timer_reverse, 10);
    nemotimer_set_timeout(anim->timer_rewind, 0);
    nemotimer_set_timeout(anim->timer_rewind_reverse, 0);
    */
    anim->reverse = true;
    anim->rewind = false;
    nemotimer_set_timeout(anim->timer, 10);
}

void animator_play_rewind(Animator *anim)
{
    /*
    nemotimer_set_timeout(anim->timer, 0);
    nemotimer_set_timeout(anim->timer_reverse, 0);
    nemotimer_set_timeout(anim->timer_rewind, 10);
    nemotimer_set_timeout(anim->timer_rewind_reverse, 0);
    */
    anim->reverse = false;
    anim->rewind = true;
    nemotimer_set_timeout(anim->timer, 10);
}

void animator_play_rewind_reverse(Animator *anim)
{
    /*
    nemotimer_set_timeout(anim->timer, 0);
    nemotimer_set_timeout(anim->timer_reverse, 0);
    nemotimer_set_timeout(anim->timer_rewind, 0);
    nemotimer_set_timeout(anim->timer_rewind_reverse, 10);
    */
    anim->reverse = true;
    anim->rewind = true;

    nemotimer_set_timeout(anim->timer, 10);
}

void animator_stop(Animator *anim)
{
    /*
    nemotimer_set_timeout(anim->timer, 0);
    nemotimer_set_timeout(anim->timer_reverse, 0);
    nemotimer_set_timeout(anim->timer_rewind, 0);
    nemotimer_set_timeout(anim->timer_rewind_reverse, 0);
    */
    nemotimer_set_timeout(anim->timer, 0);
}

void animator_play_scene(Animator *anim, AnimatorScene *scene)
{
    RET_IF(!anim);
    RET_IF(!scene);

    if (!anim->scene) {
        anim->scene = scene;
        animator_play(anim);
    } else {
        if (anim->scene != scene) {
            anim->scene_next = scene;
            animator_play_rewind(anim);
        } else {
            animator_play(anim);
        }
    }
}

void animator_play_scene_reverse(Animator *anim, AnimatorScene *scene)
{
    RET_IF(!anim);
    RET_IF(!scene);

    if (!anim->scene) {
        anim->scene = scene;
        anim->box_idx = nemoplay_box_get_count(anim->scene->box) - 1;
        animator_play_reverse(anim);
    } else {
        if (anim->scene != scene) {
            anim->scene_next = scene;
            animator_play_rewind_reverse(anim);
        } else {
            animator_play_reverse(anim);
        }
    }
}

void animator_show(Animator *anim, uint32_t easetype, int duration, int delay)
{
    nemowidget_show(anim->widget, easetype, duration, delay);
    nemowidget_set_alpha(anim->widget, easetype, duration, delay, 1.0);
    nemoshow_dispatch_frame(anim->show);
}

void animator_hide(Animator *anim, uint32_t easetype, int duration, int delay)
{
    nemowidget_hide(anim->widget, easetype, duration, delay);
    nemowidget_set_alpha(anim->widget, easetype, duration, delay, 0.0);
    nemoshow_dispatch_frame(anim->show);
}

void animator_translate(Animator *anim, uint32_t easetype, int duration, int delay, float tx, float ty)
{
    nemowidget_translate(anim->widget, easetype, duration, delay, tx, ty);
}

static void _animator_resize(NemoWidget *widget, const char *id, void *info, void *userdata)
{
    NemoWidgetInfo_Resize *resize = info;
    Animator *anim = userdata;

    struct showone *canvas = nemowidget_get_canvas(widget);

	nemoplay_shader_set_viewport(anim->shader,
			nemoshow_canvas_get_texture(canvas),
            resize->width, resize->height);

    RET_IF(!anim->scene);

    struct playone *one;
    one = nemoplay_box_get_one(anim->scene->box, anim->box_idx);
    if (one) {
        nemoplay_shader_update(anim->shader, one);
        nemoplay_shader_dispatch(anim->shader);
    }
}

static void _animator_timeout(struct nemotimer *timer, void *userdata)
{
    Animator *anim = userdata;
    AnimatorScene *scene = anim->scene;
    RET_IF(!scene);

    int box_cnt = nemoplay_box_get_count(scene->box);

    struct playone *one;
	one = nemoplay_box_get_one(scene->box, anim->box_idx);
	if (one) {
		nemoplay_shader_update(anim->shader, one);
		nemoplay_shader_dispatch(anim->shader);
	}
    //ERR("[scene: %d][box: %d/%d][%p]", list_get_idx(anim->scenes, scene), anim->box_idx, box_cnt, one);

    nemowidget_dirty(anim->widget);
    nemoshow_dispatch_frame(anim->show);

    if (!anim->rewind) {
        if (!anim->reverse) {
            anim->box_idx++;
            if (anim->box_idx >= box_cnt) {
                if (anim->scene_next) {
                    anim->box_idx = 0;
                    anim->scene = anim->scene_next;
                    anim->scene_next = NULL;
                    nemotimer_set_timeout(timer, FPS_TIMEOUT);
                } else {
                    if (scene->repeat) {
                        anim->box_idx = 0;
                        nemotimer_set_timeout(timer, FPS_TIMEOUT);
                    } else {
                        anim->box_idx--;
                        nemotimer_set_timeout(timer, 0);
                    }
                }
            } else {
                nemotimer_set_timeout(timer, FPS_TIMEOUT);
            }
        } else {
            anim->box_idx--;
            if (anim->box_idx < 0 ) {
                if (anim->scene_next) {
                    anim->box_idx = 0;
                    anim->scene = anim->scene_next;
                    anim->scene_next = NULL;
                    anim->reverse = false;
                    nemotimer_set_timeout(timer, FPS_TIMEOUT);
                } else {
                    if (scene->repeat) {
                        anim->box_idx = box_cnt - 1;
                        nemotimer_set_timeout(timer, FPS_TIMEOUT);
                    } else {
                        // XXX: repeat first scene
                        anim->box_idx = 0;
                        anim->scene = LIST_DATA(list_get_nth(anim->scenes, 0));
                        anim->reverse = false;
                        nemotimer_set_timeout(timer, FPS_TIMEOUT);
                    }
                }
            } else {
                nemotimer_set_timeout(timer, FPS_TIMEOUT);
            }
        }
    } else {
        if (!anim->reverse) {
            int speed = 15;
            anim->box_idx-= speed;

            if (anim->box_idx < 0 ) {
                anim->box_idx = 0;
                if (anim->scene_next) {
                    anim->scene = anim->scene_next;
                    anim->scene_next = NULL;
                    anim->rewind = false;
                    nemotimer_set_timeout(timer, FPS_TIMEOUT);
                } else {
                    nemotimer_set_timeout(timer, 0);
                }
            } else {
                nemotimer_set_timeout(timer, FPS_TIMEOUT);
            }
        } else {
            int speed = 5;
            anim->box_idx-= speed;

            if (anim->box_idx < 0 ) {
                anim->box_idx = 0;
                if (anim->scene_next) {
                    anim->scene = anim->scene_next;
                    anim->scene_next = NULL;
                    anim->rewind = false;
                    nemotimer_set_timeout(timer, FPS_TIMEOUT);
                } else {
                    nemotimer_set_timeout(timer, 0);
                }
            } else {
                nemotimer_set_timeout(timer, FPS_TIMEOUT);
            }
        }
    }
}

void animator_append_callback(Animator *anim, const char *id, NemoWidget_Func func, void *userdata)
{
    nemowidget_append_callback(anim->widget, id, func, userdata);
}

#if 0
void _animator_frame(NemoWidget *widget, const char *id, void *info, void *userdata)
{
    Animator *anim = userdata;
    AnimatorScene *scene = anim->scene;
    RET_IF(!scene);

    int box_cnt = nemoplay_box_get_count(scene->box);

    struct playone *one;
	one = nemoplay_box_get_one(scene->box, anim->box_idx);
	if (one) {
		nemoplay_shader_update(anim->shader, one);
		nemoplay_shader_dispatch(anim->shader);
	}
    ERR("[scene: %d][box: %d/%d][%p]", list_get_idx(anim->scenes, scene), anim->box_idx, box_cnt, one);

    nemowidget_dirty(anim->widget);
    nemoshow_dispatch_frame(anim->show);

    if (!anim->rewind) {
        if (!anim->reverse) {
            anim->box_idx++;
            if (anim->box_idx >= box_cnt) {
                if (anim->scene_next) {
                    anim->box_idx = 0;
                    anim->scene = anim->scene_next;
                    anim->scene_next = NULL;
                    nemotimer_set_timeout(timer, FPS_TIMEOUT);
                } else {
                    if (scene->repeat) {
                        anim->box_idx = 0;
                        nemotimer_set_timeout(timer, FPS_TIMEOUT);
                    } else {
                        anim->box_idx--;
                        nemotimer_set_timeout(timer, 0);
                    }
                }
            } else {
                nemotimer_set_timeout(timer, FPS_TIMEOUT);
            }
        } else {
            anim->box_idx--;
            if (anim->box_idx < 0 ) {
                if (anim->scene_next) {
                    anim->box_idx = 0;
                    anim->scene = anim->scene_next;
                    anim->scene_next = NULL;
                    anim->reverse = false;
                    nemotimer_set_timeout(timer, FPS_TIMEOUT);
                } else {
                    if (scene->repeat) {
                        anim->box_idx = box_cnt - 1;
                        nemotimer_set_timeout(timer, FPS_TIMEOUT);
                    } else {
                        // XXX: repeat first scene
                        anim->box_idx = 0;
                        anim->scene = LIST_DATA(list_get_nth(anim->scenes, 0));
                        anim->reverse = false;
                        nemotimer_set_timeout(timer, FPS_TIMEOUT);
                    }
                }
            } else {
                nemotimer_set_timeout(timer, FPS_TIMEOUT);
            }
        }
    } else {
        if (!anim->reverse) {
            int speed = 15;
            anim->box_idx-= speed;

            if (anim->box_idx < 0 ) {
                anim->box_idx = 0;
                if (anim->scene_next) {
                    anim->scene = anim->scene_next;
                    anim->scene_next = NULL;
                    anim->rewind = false;
                    nemotimer_set_timeout(timer, FPS_TIMEOUT);
                } else {
                    nemotimer_set_timeout(timer, 0);
                }
            } else {
                nemotimer_set_timeout(timer, FPS_TIMEOUT);
            }
        } else {
            int speed = 5;
            anim->box_idx-= speed;

            if (anim->box_idx < 0 ) {
                anim->box_idx = 0;
                if (anim->scene_next) {
                    anim->scene = anim->scene_next;
                    anim->scene_next = NULL;
                    anim->rewind = false;
                    nemotimer_set_timeout(timer, FPS_TIMEOUT);
                } else {
                    nemotimer_set_timeout(timer, 0);
                }
            } else {
                nemotimer_set_timeout(timer, FPS_TIMEOUT);
            }
        }
    }
}
#endif

Animator *animator_create(NemoWidget *parent, int width, int height)
{
    Animator *anim = calloc(sizeof(Animator), 1);
    anim->width = width;
    anim->height = height;
    anim->show = nemowidget_get_show(parent);
    anim->tool = nemowidget_get_tool(parent);
    anim->shader = nemoplay_shader_create();

    NemoWidget *widget;
    anim->widget = widget = nemowidget_create_opengl(parent, width, height);
    //nemowidget_append_callback(widget, "frame", _animator_frame, anim);
    nemowidget_append_callback(widget, "resize", _animator_resize, anim);
    nemowidget_set_alpha(widget, 0, 0, 0, 0.0);

    anim->timer = TOOL_ADD_TIMER(anim->tool, 0, _animator_timeout, anim);

    return anim;
}

List *__animator_scene_caches;
AnimatorScene *animator_scene_cache_pop(const char *path, bool repeat)
{
    List *l;
    AnimatorScene *cache;
    LIST_FOR_EACH(__animator_scene_caches, l, cache) {
        if (!strcmp(path, cache->path)) {
            AnimatorScene *scene = calloc(sizeof(AnimatorScene), 1);
            scene->path = strdup(path);
            scene->repeat = repeat;
            scene->play = cache->play;
            scene->box = cache->box;
            return scene;
        }
    }

    ERR("path: %s", path);
    AnimatorScene *scene = calloc(sizeof(AnimatorScene), 1);
    scene->path = strdup(path);
    scene->repeat = repeat;

    struct nemoplay *play;
    scene->play = play = nemoplay_create();
    nemoplay_load_media(scene->play, path);

    struct playbox *box;
    scene->box = box = nemoplay_box_create(nemoplay_get_video_framecount(play));
    nemoplay_extract_video(play, box, nemoplay_get_video_framecount(play));

    __animator_scene_caches = list_append(__animator_scene_caches, scene);
    return scene;
}

AnimatorScene *animator_append_scene(Animator *anim, const char *path, bool repeat)
{
    /*
    AnimatorScene *scene = calloc(sizeof(AnimatorScene), 1);
    scene->path = strdup(path);
    scene->repeat = repeat;

    struct nemoplay *play;
    scene->play = play = nemoplay_create();
    nemoplay_load_media(play, path);

    struct playbox *box;
    scene->box = box = nemoplay_box_create(nemoplay_get_video_framecount(play));
	nemoplay_extract_video(play, box);
    */

    AnimatorScene *scene = animator_scene_cache_pop(path, repeat);

    if (list_count(anim->scenes) == 0) {
        nemoplay_shader_set_format(anim->shader,
                nemoplay_get_pixel_format(scene->play));
        nemoplay_shader_resize(anim->shader,
                nemoplay_get_video_width(scene->play),
                nemoplay_get_video_height(scene->play));
    }

    anim->scenes = list_append(anim->scenes, scene);

    return scene;
}

typedef struct _LauncherView LauncherView;
typedef struct _LauncherGrab LauncherGrab;
struct _LauncherGrab
{
    bool is_longpress;
    Movie *movie;
    LauncherView *view;
};

struct _LauncherView {
    int width, height;
    struct nemoshow *show;
    struct nemotool *tool;

    NemoWidget *widget;
    struct showone *group;
    struct showone *bg;

    MovieBox *box_main_bg;
    MovieBox *box_sub_bg;
    MovieBox *box_feedback0;
    MovieBox *box_feedback1;
    MovieBox *box_feedback2;
    MovieBox *box_feedback3;
    MovieBox *box_longpress;

    List *grabs;

    List *launchers;
};

typedef struct _LauncherItem LauncherItem;
typedef struct _Launcher Launcher;

struct _LauncherItem {
    int width, height;
    Launcher *launcher;

    Movie *main_bg;
    Movie *sub_bg;
};

struct _Launcher {
    LauncherView *view;
    int x, y;
    int width, height;
    NemoWidget *parent;
    NemoWidget *widget;
    struct showone *group;

    Movie *main_bg;
    Movie *sub_bg;

    // Animator *bg;
    AnimatorScene *bg_scene;
    List *items;
};

#if 0
static void _launcher_item_event(NemoWidget *widget, const char *id, void *info, void *userdata)
{
    struct showevent *event = info;
    struct nemoshow *show = nemowidget_get_show(widget);
    LauncherItem *it = userdata;

    double ex, ey;
    nemowidget_transform_from_global(widget,
            nemoshow_event_get_x(event),
            nemoshow_event_get_y(event), &ex, &ey);

    if (nemoshow_event_is_down(show, event)) {
        ERR("down");
        animator_play_scene(it->anim, it->scene_down);
    } else if (nemoshow_event_is_up(show, event)) {
        animator_play_scene_reverse(it->anim, it->scene_down);
            //;icon_set_state(it->icon, ICON_STATE_NORMAL);
        if (nemoshow_event_is_single_click(show, event)) {
            ERR("click");
            /*
            animator_set_box_idx(it->launcher_item, 59);
            animator_set_scene(it->launcher_item, 0);
            animator_play_reverse(it->launcher_item);
            */
        } else {
            ERR("up");
            //animator_play_reverse(it->launcher_item);
        }
    }
}

void launcher_item_translate(LauncherItem *it, uint32_t easetype, int duration, int delay, int tx, int ty)
{
    animator_translate(it->anim, easetype, duration, delay, tx , ty);
}

LauncherItem *launcher_append_item(Launcher *launcher, int w, int h)
{
    LauncherItem *it = calloc(sizeof(LauncherItem), 1);
    it->launcher = launcher;
    it->width = w;
    it->height = h;

    Animator *anim;
    it->anim = anim = animator_create(launcher->parent, w, h);
    animator_append_callback(anim, "event", _launcher_item_event, it);
    it->scene_norm = animator_append_scene(anim, LAUNCHER_MOV_DIR"/launcher_norm.mov", true);
    it->scene_hide = animator_append_scene(anim, LAUNCHER_MOV_DIR"/launcher_show.mov", false);
    // FIXME: revrese
    animator_scene_make_reverse(it->scene_hide);
    it->scene_down = animator_append_scene(anim, LAUNCHER_MOV_DIR"/launcher_down.mov", false);

    launcher->items = list_append(launcher->items, it);

    return it;
}

void launcher_item_show(LauncherItem *it, uint32_t easetype, int duration, int delay)
{
    animator_show(it->anim, easetype, duration, delay);
    animator_play_scene_reverse(it->anim, it->scene_hide);
}
#endif

Launcher *launcher_create(LauncherView *view)
{
    int w, h;
    Launcher *launcher = calloc(sizeof(Launcher), 1);
    launcher->view = view;
    launcher->width = w = view->box_main_bg->width;
    launcher->height = h = view->box_main_bg->height;

    ERR("%d %d", w, h);
    launcher->main_bg = movie_create(view->widget, w, h);
    launcher->sub_bg = movie_create(view->widget, w, h);

    return launcher;
}

void launcher_translate(Launcher *launcher, uint32_t easetype, int duration, int delay, int tx, int ty)
{
    movie_translate(launcher->main_bg, easetype, duration, delay, tx, ty);
    /*
    launcher->x = tx;
    launcher->y = ty;
    nemowidget_translate(launcher->widget, easetype, duration, delay, tx, ty);
    movie_translate(launcher->sub_bg, easetype, duration, delay, tx, ty);
    */
    //animator_translate(launcher->bg, easetype, duration, delay, tx, ty);
}

void launcher_show(Launcher *launcher, uint32_t easetype, int duration, int delay)
{
    movie_play(launcher->main_bg, launcher->view->box_main_bg, true);
    /*
    animator_show(launcher->bg, NEMOEASE_CUBIC_INOUT_TYPE, 1000, 0);
    animator_play_scene(launcher->bg, launcher->bg_scene);
    */

#if 0
    List *l;
    LauncherItem *it;

    it = LIST_DATA(LIST_FIRST(launcher->items));
    RET_IF(!it);

    int ih = it->height;
    int gap = ih/4;
    int cnt = list_count(launcher->items);

    int start = (launcher->height - (cnt * ih + (cnt - 1) * gap))/2;
    LIST_FOR_EACH(launcher->items, l, it) {
        launcher_item_translate(it, NEMOEASE_CUBIC_INOUT_TYPE, 500, 0,
                launcher->x, launcher->y + start);
        launcher_item_show(it, NEMOEASE_CUBIC_INOUT_TYPE, 1000, 0);
        start += ih + gap;
    }
#endif
}

#if 0
static void _launcher_icon_event(NemoWidget *widget, const char *id, void *info, void *userdata)
{
    struct showevent *event = info;
    struct nemoshow *show = nemowidget_get_show(widget);
    LauncherView *view = userdata;

    double ex, ey;
    nemowidget_transform_from_global(widget,
            nemoshow_event_get_x(event),
            nemoshow_event_get_y(event), &ex, &ey);

    if (nemoshow_event_is_down(show, event)) {
        animator_set_scene(view->launcher_item0, 0);
        animator_set_reverse(view->launcher_item0, false);
        animator_play(view->launcher_item0);
        animator_show(view->launcher_item0, NEMOEASE_CUBIC_OUT_TYPE, 150, 0);
        animator_hide(view->launcher_item1, NEMOEASE_CUBIC_OUT_TYPE, 150, 0);
        animator_hide(view->launcher_item, NEMOEASE_CUBIC_OUT_TYPE, 150, 0);

    } else if (nemoshow_event_is_up(show, event)) {
        animator_set_scene(view->launcher_item1, 0);
        animator_set_reverse(view->launcher_item1, true);
        animator_play(view->launcher_item1);
        animator_hide(view->launcher_item0, NEMOEASE_CUBIC_OUT_TYPE, 150, 0);
        animator_show(view->launcher_item1, NEMOEASE_CUBIC_OUT_TYPE, 150, 0);
        animator_hide(view->launcher_item, NEMOEASE_CUBIC_OUT_TYPE, 150, 0);

        if (nemoshow_event_is_single_click(show, event)) {
        }
    }
}
#endif

static void _launcherview_grab_event(NemoWidgetGrab *grab, NemoWidget *widget, struct showevent *event, void *userdata)
{
    struct nemoshow *show = nemowidget_get_show(widget);
    double ex, ey;
    nemowidget_transform_from_global(widget,
            nemoshow_event_get_x(event),
            nemoshow_event_get_y(event), &ex, &ey);
    LauncherGrab *lgrab = userdata;
    Movie *movie = lgrab->movie;
    LauncherView *view = lgrab->view;

    if (nemoshow_event_get_grab_time(event) + 1000 < nemoshow_event_get_time(event)) {
        if (!lgrab->is_longpress) {
            lgrab->is_longpress = true;

            Launcher *launcher;
            LauncherItem *it;
            launcher = launcher_create(view);
            launcher_show(launcher, NEMOEASE_CUBIC_INOUT_TYPE, 500, 0);
            launcher_translate(launcher, 0, 0, 0, ex - view->box_main_bg->width/2, 0);
            view->launchers = list_append(view->launchers, launcher);

            /*
            int i = 0;
            for (i = 0 ; i < 10 ; i++) {
                launcher_append_item(launcher, view->launcher_iw, view->launcher_ih);
            }
            launcher_translate(launcher, 0, 0, 0, ex - view->launcher_w/2, 0);
            launcher_show(launcher, NEMOEASE_CUBIC_INOUT_TYPE, 1000, 0);
            */

        }
    }

    if (nemoshow_event_is_down(show, event)) {
        int r = rand()%4;
        if (r == 0) {
            movie_play(movie, view->box_feedback0, true);
        } else if (r == 1) {
            movie_play(movie, view->box_feedback1, true);
        } else if (r == 2) {
            movie_play(movie, view->box_feedback2, true);
        } else {
            movie_play(movie, view->box_feedback3, true);
        }
        movie_translate(movie, 0, 0, 0, ex - movie->width/2, ey - movie->height/2);
    } else if (nemoshow_event_is_motion(show, event)) {
        movie_translate(movie, 0, 0, 0, ex - movie->width/2, ey - movie->height/2);
    } else if (nemoshow_event_is_up(show, event)) {
        movie_translate(movie, 0, 0, 0, ex - movie->width/2, ey - movie->height/2);
        movie_rewind(movie);
        view->grabs = list_remove(view->grabs, lgrab);
    }
}

static void _launcherview_event(NemoWidget *widget, const char *id, void *info, void *userdata)
{
    struct showevent *event = info;
    struct nemoshow *show = nemowidget_get_show(widget);
    double ex, ey;
    nemowidget_transform_from_global(widget,
            nemoshow_event_get_x(event),
            nemoshow_event_get_y(event), &ex, &ey);
    LauncherView *view = userdata;

    if (nemoshow_event_is_down(show, event)) {
        LauncherGrab *lgrab = calloc(sizeof(LauncherGrab), 1);
        lgrab->movie = movie_create(widget,
                view->box_feedback0->width,
                view->box_feedback0->height);
        lgrab->view = view;
        nemowidget_create_grab(widget, event, _launcherview_grab_event, lgrab);
        view->grabs = list_append(view->grabs, lgrab);
    }
}

LauncherView *launcherview_create(NemoWidget *parent, int width, int height, ConfigApp *app)
{
    LauncherView *view = calloc(sizeof(LauncherView), 1);
    view->show = nemowidget_get_show(parent);
    view->tool = nemowidget_get_tool(parent);
    view->width = width;
    view->height = height;

    NemoWidget *widget;
    struct showone *one;
    struct showone *group;

    view->widget = widget = nemowidget_create_vector(parent, width, height);
    nemowidget_append_callback(widget, "event", _launcherview_event, view);
    nemowidget_set_alpha(widget, 0, 0, 0, 1.0);

    view->group = group = GROUP_CREATE(nemowidget_get_canvas(widget));

    const char *main_bg_path, *sub_bg_path;
    const char *feedback0, *feedback1, *feedback2, *feedback3, *longpress;
    if (!strcmp(app->type, "wall")) {
        if (width == 2160) {
            main_bg_path = LAUNCHER_MOV_DIR"/bg/wall/4k/main.mov";
            sub_bg_path = LAUNCHER_MOV_DIR"/bg/wall/4k/sub.mov";
            feedback0 = LAUNCHER_MOV_DIR"/effect/4k/feedback0.mov";
            feedback1 = LAUNCHER_MOV_DIR"/effect/4k/feedback1.mov";
            feedback2 = LAUNCHER_MOV_DIR"/effect/4k/feedback2.mov";
            feedback3 = LAUNCHER_MOV_DIR"/effect/4k/feedback3.mov";
            longpress = LAUNCHER_MOV_DIR"/effect/4k/longpress.mov";
        } else {
            main_bg_path = LAUNCHER_MOV_DIR"/bg/wall/1k/main.mov";
            sub_bg_path = LAUNCHER_MOV_DIR"/bg/wa1l/1k/sub.mov";
            feedback0 = LAUNCHER_MOV_DIR"/effect/1k/feedback0.mov";
            feedback1 = LAUNCHER_MOV_DIR"/effect/1k/feedback1.mov";
            feedback2 = LAUNCHER_MOV_DIR"/effect/1k/feedback2.mov";
            feedback3 = LAUNCHER_MOV_DIR"/effect/1k/feedback3.mov";
            longpress = LAUNCHER_MOV_DIR"/effect/1k/longpress.mov";
        }
    } else {
        if (width == 2160) {
            main_bg_path = LAUNCHER_MOV_DIR"/bg/table/4k/main.mov";
            sub_bg_path = LAUNCHER_MOV_DIR"/bg/table/4k/sub.mov";
            feedback0 = LAUNCHER_MOV_DIR"/effect/4k/feedback0.mov";
            feedback1 = LAUNCHER_MOV_DIR"/effect/4k/feedback1.mov";
            feedback2 = LAUNCHER_MOV_DIR"/effect/4k/feedback2.mov";
            feedback3 = LAUNCHER_MOV_DIR"/effect/4k/feedback3.mov";
            longpress = LAUNCHER_MOV_DIR"/effect/4k/longpress.mov";
        } else {
            main_bg_path = LAUNCHER_MOV_DIR"/bg/table/1k/main.mov";
            sub_bg_path = LAUNCHER_MOV_DIR"/bg/table/1k/sub.mov";
            feedback0 = LAUNCHER_MOV_DIR"/effect/1k/feedback0.mov";
            feedback1 = LAUNCHER_MOV_DIR"/effect/1k/feedback1.mov";
            feedback2 = LAUNCHER_MOV_DIR"/effect/1k/feedback2.mov";
            feedback3 = LAUNCHER_MOV_DIR"/effect/1k/feedback3.mov";
            longpress = LAUNCHER_MOV_DIR"/effect/1k/longpress.mov";
        }
    }

    view->box_main_bg = movie_box_create(main_bg_path);
    view->box_sub_bg = movie_box_create(sub_bg_path);
    view->box_feedback0 = movie_box_create(feedback0);
    view->box_feedback1 = movie_box_create(feedback1);
    view->box_feedback2 = movie_box_create(feedback2);
    view->box_feedback3 = movie_box_create(feedback3);
    view->box_longpress = movie_box_create(longpress);

    return view;
}

void launcherview_show(LauncherView *view)
{
    nemowidget_show(view->widget, 0, 0, 0);
    nemowidget_set_alpha(view->widget, 0, 0, 0, 1.0);
    nemoshow_dispatch_frame(view->show);
}

void launcherview_hide(LauncherView *view)
{
    nemowidget_hide(view->widget, 0, 0, 0);
    nemowidget_set_alpha(view->widget, 0, 0, 0, 0.0);
    nemoshow_dispatch_frame(view->show);
}

int main(int argc, char *argv[])
{
    ConfigApp *app = _config_load(PROJECT_NAME, APPNAME, CONFXML, argc, argv);
    RET_IF(!app, -1);

    struct nemotool *tool = TOOL_CREATE();
    NemoWidget *win = nemowidget_create_win_base(tool, APPNAME, app->config);
    nemowidget_win_set_anchor(win, 0, 0);
    nemowidget_win_set_layer(win, "underlay");
    nemowidget_win_enable_move(win, 0);
    nemowidget_win_enable_rotate(win, 0);
    nemowidget_win_enable_scale(win, 0);

    LauncherView *view = launcherview_create(win, app->config->width, app->config->height, app);
    launcherview_show(view);

    nemowidget_show(win, 0, 0, 0);
    nemotool_run(tool);

    nemowidget_destroy(win);
    TOOL_DESTROY(tool);
    _config_unload(app);

    return 0;
}
