#include "classes.h"

extern void edid_to_string(uint8_t *edid, int length, char **edid_string);
extern void for_each_output(
	void *self,
	xcb_randr_get_screen_resources_reply_t *screen_resources_reply,
	void (*callback)(void *,xcb_randr_output_t *)
);
extern void load_class_constructor(load_class *,screen_class *,config_t *);
extern int VERBOSE;

void autoload_constructor(autoload_class *,screen_class *,config_t *);
static void match_with_profile(void *,xcb_randr_output_t *);
static void wait_for_event(autoload_class *);
static void find_profile_and_load(autoload_class *);