#include "common.h"
#include "umonitor.h"
#include "load.h"


#define PVAR ((load_pvar *) (self->pvar))

/*! Queue of crtcs to be loaded*/
typedef struct _set_crtc_param {
	xcb_randr_crtc_t crtc;	/*!< crtc to be loaded */
	int pos_x;		/*!< x position of crtc */
	int pos_y;		/*!< y position of crtc */
	int is_primary;		/*!< Whether or not the output associated with this
				   crtc is the primary output */
	xcb_randr_mode_t mode_id;	/*!< mode id of crtc */
	xcb_randr_output_t *output_p;	/*!< to which output the crtc should be
					   connected */
	struct _set_crtc_param *next;	/*!< next crtc param */
	struct _set_crtc_param *prev;	/*!< previous crtc param */

} set_crtc_param;

typedef struct _crtc_ll {
	xcb_randr_crtc_t crtc;
	struct _crtc_ll *next;
} crtc_ll;

/*! Screen information that is saved into configuration file
*/
typedef struct {
	int width;		/*!< the width of the screen in pixels */
	int height;		/*!< the height of the screen in pixels */
	int widthMM;		/*!< the width of the screen in mm */
	int heightMM;		/*!< the height of the screen in mm */
} umon_setting_screen_t;

/*! Output information that is saved into configuration file
*/
typedef struct {
	const char *edid_val;	/*!< the edid value as a string */
	int pos_x;		/*!< the output x coordinate */
	int pos_y;		/*!< the output y coordinate */
	int res_x;		/*!< the output x resolution */
	int res_y;		/*!< the output y resolution */
	int crtc_id;		/*!< the output's crtc id */
	int mode_id;		/*!< the output's mode id */
	int is_primary;		/*!< whether or not the output is the primary output */
} umon_setting_output_t;

/*! Settings that are stored for one profile*/
typedef struct {
	umon_setting_screen_t screen;	/*!< the screen settings */
	umon_setting_output_t *outputs;	/*!< the output settings */
} umon_setting_val_t;

struct modify_crtc_ll_param_t {
	int *i_p;
	xcb_randr_crtc_t *crtcs_p;	/*!< crtcs associated with the screen */
	set_crtc_param **crtc_param_head_p;	/*!< head of crtc queue */
	crtc_ll **disable_crtc_head_p;
};

struct add_disable_crtc_param_t {
	int *i_p;
	xcb_randr_crtc_t *crtcs_p;	/*!< crtcs associated with the screen */
	crtc_ll **disable_crtc_head_p;

};

struct remove_matching_crtc_from_ll_param_t {
	int *should_disable_p;

	set_crtc_param **cur_crtc_param_p;
	xcb_randr_get_crtc_info_reply_t *crtc_info_reply;
	xcb_randr_output_t *conn_output;
	set_crtc_param **crtc_param_head_p;	/*!< head of crtc queue */
};

struct determine_crtc_match_param_t {
	int i,j;
	xcb_randr_crtc_t * crtc_p;
};

static void load_profile(load_class * self,
			 config_setting_t * profile_group, int);
static void match_with_config(void *self_void,
			      xcb_randr_output_t * output_p);
static void find_mode_id(load_class * self);
static void load_config_val(load_class * self, config_setting_t * profile_group);
static void find_available_crtc(load_class *, xcb_randr_crtc_t *);
static void apply_settings(load_class *);
static void get_cur_loaded(load_class * self, int *cur_loaded);
static void free_crtc_ll(crtc_ll * crtc_ll_head);
static void modify_crtc_ll(load_class * self, int i);
static void add_disable_crtc(load_class * self, int i);
static void remove_matching_crtc_from_ll(load_class * self);
static void apply_settings_disable_crtcs(load_class * self);
static void apply_settings_screen_size(load_class * self);
static void apply_settings_enable_crtcs(load_class * self);
static void get_last_time(load_class * self, xcb_timestamp_t * last_time);
static void determine_mode_id_match(load_class * self,
				    xcb_randr_mode_info_iterator_t
				    mode_info_iterator, int j);
static void add_crtc_param(load_class * self, xcb_randr_mode_info_iterator_t mode_info_iterator);
static void determine_crtc_match(load_class * self, struct determine_crtc_match_param_t *);
static void free_crtc_param_ll(set_crtc_param * head);


/*! \file
    \brief Load a profile and apply the changes to the current
configuration (if needed)

*/

typedef struct {
	screen_class *screen_o;	/*!< Connection information to server */

	// Variables
	/*! contains the profile information that is loaded from the conf file */
	umon_setting_val_t umon_setting_val;
	/*! current output info */
	/*! current output id */
	xcb_randr_output_t *cur_output;


	int conf_output_idx;	/*!< current output index */
	int num_out_pp;		/*!< number of outputs per profile */
	int crtc_offset;	/*!< how many available crtcs to skip before returning */
	int cur_loaded;		/*!< whether or not current profile is loaded */
	int already_assigned;
	xcb_timestamp_t last_time;	/*!< last time the crtc setting was changed */
	xcb_randr_crtc_t *available_crtcs;
	xcb_randr_crtc_t *output_crtcs;
	crtc_ll *assigned_crtc_head;	/*!< disable crtcs linked list */
	xcb_randr_mode_t *output_modes;

} load_pvar;
// Inheriting classes



/*! load constructor*/

void load_class_constructor(load_class ** self_p, screen_class * screen_o)
{
	load_class *self;

	self = (load_class *) malloc(sizeof(load_class));
	self->pvar = (void *) malloc(sizeof(load_pvar));

	self->load_profile = load_profile;
	self->get_last_time = get_last_time;
	self->get_cur_loaded = get_cur_loaded;

	PVAR->screen_o = screen_o;
	PVAR->crtc_param_head = NULL;
	PVAR->umon_setting_val.outputs = NULL;
	PVAR->disable_crtc_head = NULL;
	PVAR->assigned_crtc_head = NULL;
	PVAR->last_time = (xcb_timestamp_t) 0;

	*self_p = self;

}

/*! load deconstructor*/

void load_class_destructor(load_class * self)
{

	free(PVAR->umon_setting_val.outputs);
	free(PVAR->output_info_reply);

	free(self);

}

/*! \brief Load the specified profile
 */

static void load_profile(load_class * self,
			 config_setting_t * profile_group, int test_cur)
{
	int i;
	struct modify_crtc_ll_param_t modify_crtc_ll_param;
	crtc_ll *disable_crtc_head;
	set_crtc_param *crtc_param_head;
	xcb_randr_crtc_t *crtcs_p;

	load_config_val(self,profile_group);
	for_each_output((void *) self,
			PVAR->screen_o->screen_resources_reply,
			match_with_config);
	// Check against redundant loading
	//TODO Only assuming 1 output connected to crtc
	crtcs_p =
	    xcb_randr_get_screen_resources_crtcs(PVAR->
						 screen_o->screen_resources_reply);

	modify_crtc_ll_param.i_p = &i;
	modify_crtc_ll_param.crtcs_p = crtcs_p;
	modify_crtc_ll_param.crtc_param_head_p = &crtc_param_head;
	modify_crtc_ll_param.disable_crtc_head_p = &disable_crtc_head;
	for (i = 0;
	     i < PVAR->screen_o->screen_resources_reply->num_crtcs; ++i) {
		modify_crtc_ll(self, &modify_crtc_ll_param);
	}
	if ((crtc_param_head == NULL) &&
	    (disable_crtc_head == NULL)) {
		PVAR->cur_loaded = 1;
	} else {
		if (!test_cur) {
			apply_settings(self,crtc_param_head,disable_crtc_head);
		} else {
			free_crtc_param_ll(crtc_param_head);
			free_crtc_ll(disable_crtc_head);
		}
		PVAR->cur_loaded = 0;
	}
	crtc_param_head = NULL;
	disable_crtc_head = NULL;
	// TODO should move assigned crtc head to more appropriate location
	free_crtc_ll(PVAR->assigned_crtc_head);
	assigned_crtc_head = NULL;

}


static void get_cur_loaded(load_class * self, int *cur_loaded)
{
	*cur_loaded = PVAR->cur_loaded;
}

static void free_crtc_param_ll(set_crtc_param * head)
{
	set_crtc_param *cur_free_crtc, *old_free_crtc;
	cur_free_crtc = head;
	while (cur_free_crtc) {
		old_free_crtc = cur_free_crtc;
		cur_free_crtc = cur_free_crtc->next;
		free(old_free_crtc);
	}
}

static void free_crtc_ll(crtc_ll * crtc_ll_head)
{
	crtc_ll *cur_free_crtc, *old_free_crtc;
	cur_free_crtc = crtc_ll_head;
	while (cur_free_crtc) {
		old_free_crtc = cur_free_crtc;
		cur_free_crtc = cur_free_crtc->next;
		free(old_free_crtc);
	}
}

static void modify_crtc_ll(load_class * self, struct modify_crtc_ll_param_t *param)
{
	int i,should_disable;
	xcb_randr_get_crtc_info_cookie_t crtc_info_cookie;
	xcb_randr_get_crtc_info_reply_t *crtc_info_reply;
	xcb_randr_output_t *conn_output;
	set_crtc_param *cur_crtc_param;
	struct remove_matching_crtc_from_ll_param_t remove_crtc_param;
	struct add_disable_crtc_param_t add_disable_param;

	i = *(param->i_p);
	should_disable = 1;
	crtc_info_cookie =
	    xcb_randr_get_crtc_info(PVAR->screen_o->c,
				    param->crtcs_p[i], XCB_CURRENT_TIME);
	crtc_info_reply =
	    xcb_randr_get_crtc_info_reply(PVAR->screen_o->c,
					  crtc_info_cookie,
					  &PVAR->screen_o->e);
	conn_output =
	    xcb_randr_get_crtc_info_outputs(crtc_info_reply);
	cur_crtc_param = param->crtc_param_head;

	remove_crtc_param.should_disable_p = &should_disable;
	remove_crtc_param.cur_crtc_param_p = &cur_crtc_param;
	remove_crtc_param.crtc_param_head_p = param->crtc_param_head_p;
	remove_crtc_param.crtc_info_reply = crtc_info_reply;
	remove_crtc_param.conn_output = conn_output;
	while (cur_crtc_param) {
		remove_matching_crtc_from_ll(self,&remove_crtc_param);
	}

	add_disable_param.i_p = param->i_p;
	add_disable_param.crtcs_p = param->crtcs_p;
	add_disable_param.disable_crtc_head_p = param->disable_crtc_head_p;
	if (should_disable && crtc_info_reply->mode) {
		add_disable_crtc(self, add_disable_param);
	}
	umon_print("Crtc %d should be disabled? %d\n",
		   param->crtcs_p[i], should_disable);
	free(crtc_info_reply);

}

static void add_disable_crtc(load_class * self, struct add_disable_crtc_param_t *param)
{
	int i;
	crtc_ll *new_disable_crtc;

	i = *(param->i_p);
	new_disable_crtc = (crtc_ll *) malloc(sizeof(crtc_ll));
	new_disable_crtc->crtc = param->crtcs_p[i];
	new_disable_crtc->next = *(param->disable_crtc_head_p);
	*(param->disable_crtc_head_p) = new_disable_crtc;

}

static void remove_matching_crtc_from_ll(load_class * self, remove_matching_crtc_from_ll_param_t *param)
{
	set_crtc_param *old_crtc_param, *cur_crtc_param;

	cur_crtc_param = *(param->cur_crtc_param_p);
	if (cur_crtc_param->pos_x == param->crtc_info_reply->x &&
	    cur_crtc_param->pos_y == param->crtc_info_reply->y &&
	    cur_crtc_param->mode_id ==
	    param->crtc_info_reply->mode
	    && cur_crtc_param->output_p[0] == param->conn_output[0]) {
		*(param->should_disable_p) = 0;
		//Remove current crtc_param from linked list
		//printf("Remove current crtc_param from ll\n");
		if (cur_crtc_param->prev) {
			cur_crtc_param->prev->next =
			    cur_crtc_param->next;
		} else {
			*(param->crtc_param_head_p) = cur_crtc_param->next;
		}
		if (cur_crtc_param->next)
			cur_crtc_param->next->prev =
			    cur_crtc_param->prev;
		old_crtc_param = cur_crtc_param;
		*(param->cur_crtc_param_p) = cur_crtc_param->next;
		free(old_crtc_param);
	} else {
		*(param->cur_crtc_param_p) = PVAR->cur_crtc_param->next;
	}


}

/*! \brief Set the crtcs to the queued up configuration stored in the
linked list crtc_param_head */

static void apply_settings(load_class * self, set_crtc_param *crtc_param_head, crtc_ll * disable_crtc_head)
{

	apply_settings_disable_crtcs(self, disable_crtc_head);

	apply_settings_screen_size(self);
	//TODO implement
	//find_duplicate_crtc(self);
	apply_settings_enable_crtcs(self, crtc_param_head);

}

static void apply_settings_disable_crtcs(load_class * self, crtc_ll * disable_crtc_head)
{
	xcb_randr_set_crtc_config_cookie_t crtc_config_cookie;
	xcb_randr_set_crtc_config_reply_t *crtc_config_reply;
	crtc_ll *cur_disable_crtc, *old_disable_crtc;

	umon_print("Disable crtcs here\n");
	if (!disable_crtc_head)
		return;
	cur_disable_crtc = disable_crtc_head;
	while (cur_disable_crtc) {
		umon_print("Disabling this crtc: %d\n",
			   cur_disable_crtc->crtc);
		crtc_config_cookie =
		    xcb_randr_set_crtc_config(PVAR->screen_o->c,
					      cur_disable_crtc->crtc,
					      XCB_CURRENT_TIME,
					      XCB_CURRENT_TIME, 0,
					      0, XCB_NONE,
					      XCB_RANDR_ROTATION_ROTATE_0,
					      0, NULL);

		old_disable_crtc = cur_disable_crtc;
		cur_disable_crtc = cur_disable_crtc->next;
		free(old_disable_crtc);
	}
	xcb_flush(PVAR->screen_o->c);
	crtc_config_reply =
	    xcb_randr_set_crtc_config_reply(PVAR->screen_o->c,
					    crtc_config_cookie,
					    &PVAR->screen_o->e);
	PVAR->last_time = crtc_config_reply->timestamp;
	free(crtc_config_reply);

}

static void apply_settings_screen_size(load_class * self)
{
	umon_print("Change screen size here\n");
	xcb_randr_set_screen_size(PVAR->screen_o->c,
				  PVAR->screen_o->screen->root,
				  (uint16_t) PVAR->umon_setting_val.
				  screen.width,
				  (uint16_t) PVAR->umon_setting_val.
				  screen.height,
				  (uint32_t) PVAR->umon_setting_val.
				  screen.widthMM,
				  (uint32_t) PVAR->umon_setting_val.
				  screen.heightMM);
	//xcb_flush(PVAR->screen_o->c);

}

static void apply_settings_enable_crtcs(load_class * self,set_crtc_param * crtc_param_head)
{
	xcb_randr_set_crtc_config_cookie_t crtc_config_cookie;
	xcb_randr_set_crtc_config_reply_t *crtc_config_reply;
	set_crtc_param *cur_crtc_param, *old_crtc_param;

	if (!crtc_param_head)
		return;
	cur_crtc_param = crtc_param_head;
	while (cur_crtc_param) {

		crtc_config_cookie =
		    xcb_randr_set_crtc_config(PVAR->screen_o->c,
					      cur_crtc_param->crtc,
					      XCB_CURRENT_TIME,
					      XCB_CURRENT_TIME,
					      cur_crtc_param->pos_x,
					      cur_crtc_param->pos_y,
					      cur_crtc_param->mode_id,
					      XCB_RANDR_ROTATION_ROTATE_0,
					      1, cur_crtc_param->output_p);

		if (cur_crtc_param->is_primary) {
			xcb_randr_set_output_primary
			    (PVAR->screen_o->c,
			     PVAR->screen_o->screen->root,
			     *(cur_crtc_param->output_p));
		}
		old_crtc_param = cur_crtc_param;
		cur_crtc_param = cur_crtc_param->next;
		free(old_crtc_param);
	}
	umon_print("Enable crtcs here\n");

	crtc_config_reply =
	    xcb_randr_set_crtc_config_reply(PVAR->screen_o->c,
					    crtc_config_cookie,
					    &PVAR->screen_o->e);
	PVAR->last_time = crtc_config_reply->timestamp;

	free(crtc_config_reply);

}

static void get_last_time(load_class * self, xcb_timestamp_t * last_time)
{
	*last_time = PVAR->last_time;
}



/*! \brief For each connected output, find the configuration monitor edid that
matches connected monitor edid */

static void match_with_config(void *self_void,
			      xcb_randr_output_t * output_p)
{

	int i;
	xcb_randr_get_output_info_cookie_t output_info_cookie;
	xcb_randr_get_output_info_reply_t *output_info_reply;

	load_class *self = (load_class *) self_void;

	char *edid_string;

	output_info_cookie =
	    xcb_randr_get_output_info(PVAR->screen_o->c, *output_p,
				      XCB_CURRENT_TIME);

	output_info_reply =
	    xcb_randr_get_output_info_reply(PVAR->screen_o->c,
					    output_info_cookie,
					    &PVAR->screen_o->e);

	fetch_edid(PVAR->cur_output, PVAR->screen_o, &edid_string);

	umon_print("Num outputs per profile: %d\n", PVAR->num_out_pp);

	for (i = 0; i < PVAR->num_out_pp; ++i) {
		if (!strcmp
		    (PVAR->umon_setting_val.
		     outputs[i].edid_val,
		     edid_string)) {
			//if (VERBOSE) printf("Before for each output mode\n");
			// Which crtc has the same resolution?
			find_mode_id(self);
			//if (VERBOSE) printf("Finish find_mode_id\n");
		}
	}
	free(edid_string);
}

/*! \brief After matching edid has been found, find the matching mode id from
the configuration's resolution x and y values

  Queues up the crtc to be loaded in apply_settings
 */

static void find_mode_id(load_class * self)
{

	int num_screen_modes, num_output_modes, i, j;

	xcb_randr_mode_info_iterator_t mode_info_iterator;

	mode_info_iterator =
	    xcb_randr_get_screen_resources_modes_iterator
	    (PVAR->screen_o->screen_resources_reply);
	num_screen_modes =
	    xcb_randr_get_screen_resources_modes_length(PVAR->
							screen_o->screen_resources_reply);

	PVAR->output_modes =
	    xcb_randr_get_output_info_modes(PVAR->output_info_reply);
	num_output_modes =
	    xcb_randr_get_output_info_modes_length
	    (PVAR->output_info_reply);

	if ((PVAR->umon_setting_val.outputs[PVAR->conf_output_idx].res_x ==
	     0)
	    || (PVAR->umon_setting_val.
		outputs[PVAR->conf_output_idx].res_y == 0))
		return;

	for (i = 0; i < num_screen_modes; ++i) {
		for (j = 0; j < num_output_modes; ++j) {
			determine_mode_id_match(self, mode_info_iterator,
						j);
		}
		xcb_randr_mode_info_next(&mode_info_iterator);

	}

	//if (VERBOSE) printf("Found current mode id\n");
}

static void determine_mode_id_match(load_class * self,
				    xcb_randr_mode_info_iterator_t
				    mode_info_iterator, int j)
{

	if ((mode_info_iterator.data->width !=
	     PVAR->umon_setting_val.outputs[PVAR->conf_output_idx].res_x)
	    || (mode_info_iterator.data->height !=
		PVAR->umon_setting_val.outputs[PVAR->
					       conf_output_idx].res_y)
	    || (mode_info_iterator.data->id != PVAR->output_modes[j]))
		return;
	//if (VERBOSE) printf("Found current mode info\n");
	//sprintf(res_string,"%dx%d",mode_info_iterator.data->width,mode_info_iterator.data->height);
	add_crtc_param(self,mode_info_iterator);


}

static void add_crtc_param(load_class * self, xcb_randr_mode_info_iterator_t mode_info_iterator)
{
	set_crtc_param *new_crtc_param;

	new_crtc_param = (set_crtc_param *)
	    malloc(sizeof(set_crtc_param));
	find_available_crtc(self, &(new_crtc_param->crtc));
	umon_print("Queing up crtc to load: %d\n", new_crtc_param->crtc);
	new_crtc_param->pos_x =
	    PVAR->umon_setting_val.outputs[PVAR->conf_output_idx].pos_x;
	new_crtc_param->pos_y =
	    PVAR->umon_setting_val.outputs[PVAR->conf_output_idx].pos_y;
	new_crtc_param->is_primary =
	    PVAR->umon_setting_val.outputs[PVAR->
					   conf_output_idx].is_primary;
	new_crtc_param->mode_id = mode_info_iterator.data->id;
	new_crtc_param->output_p = PVAR->cur_output;
	new_crtc_param->next = PVAR->crtc_param_head;

	new_crtc_param->prev = NULL;
	//printf("did I make it here\n");

	PVAR->crtc_param_head = new_crtc_param;
	//printf("PVAR->crtc_param_head->next: %d\n",PVAR->crtc_param_head->next);
	if (PVAR->crtc_param_head->next)
		PVAR->crtc_param_head->next->prev = new_crtc_param;

}


/*! \brief Find an available crtc for the current output with a specified offset
 */


static void find_available_crtc(load_class * self,
				xcb_randr_crtc_t * crtc_p)
{
	struct determine_crtc_match_param_t determine_crtc_match_param;
	int i,j,already_assigned;

	determine_crtc_match_param.crtc_p = crtc_p;
	PVAR->available_crtcs =
	    xcb_randr_get_screen_resources_crtcs(PVAR->
						 screen_o->screen_resources_reply);

	PVAR->output_crtcs =
	    xcb_randr_get_output_info_crtcs(PVAR->output_info_reply);

	int num_output_crtcs =
	    xcb_randr_get_output_info_crtcs_length
	    (PVAR->output_info_reply);

	int num_available_crtcs =
	    PVAR->screen_o->screen_resources_reply->num_crtcs;

	// if (VERBOSE) printf("num_available_crtcs: %d\n",num_available_crtcs);
	// if (VERBOSE) printf("num_output_crtcs: %d\n",num_output_crtcs);

	for (i = 0; i < num_available_crtcs; ++i) {
		determine_crtc_match_param.i = i;
		for (j = 0; j < num_output_crtcs; ++j) {
			determine_crtc_match_param.j = j;
			determine_crtc_match(self, &determine_crtc_match_param);
			if (!PVAR->already_assigned)
				return;
		}
	}

	exit(NO_AVAILABLE_CRTC);

}

static void determine_crtc_match(load_class * self, struct determine_crtc_match_param_t *determine_crtc_match_param)
{
	crtc_ll *new_assigned_crtc, *cur_assigned_crtc;

	if (PVAR->available_crtcs[determine_crtc_match_param->i] != PVAR->output_crtcs[determine_crtc_match_param->j])
		return;

	PVAR->already_assigned = 0;
	umon_print
	    ("Found potential crtc %d. Is it already assigned?\n",
	     PVAR->available_crtcs[determine_crtc_match_param->i]);
	for (cur_assigned_crtc =
	     PVAR->assigned_crtc_head;
	     cur_assigned_crtc;
	     cur_assigned_crtc = cur_assigned_crtc->next) {
		umon_print
		    ("cur_assigned_crtc %d\n", cur_assigned_crtc->crtc);
		if (cur_assigned_crtc->crtc == PVAR->available_crtcs[determine_crtc_match_param->i]) {
			umon_print
			    ("Crtc %d already assigned!\n",
			     PVAR->available_crtcs[determine_crtc_match_param->i]);
			PVAR->already_assigned = 1;
		}
	}
	if (!PVAR->already_assigned) {
		new_assigned_crtc = (crtc_ll *)
		    malloc(sizeof(crtc_ll));
		new_assigned_crtc->crtc = PVAR->available_crtcs[determine_crtc_match_param->i];
		new_assigned_crtc->next = PVAR->assigned_crtc_head;
		PVAR->assigned_crtc_head = new_assigned_crtc;
		*(determine_crtc_match_param->crtc_p) = PVAR->available_crtcs[determine_crtc_match_param->i];
	}

}

/*! \brief Load the values stored in the configuration file into this program
 */
static void load_config_val(load_class * self, config_setting_t * profile_group)
{

	config_setting_t *mon_group, *group, *res_group, *pos_group;
	int i;
	mon_group = config_setting_lookup(profile_group, "Monitors");
	// printf("Checking group %d\n",mon_group);
	PVAR->num_out_pp = config_setting_length(mon_group);
	free(PVAR->umon_setting_val.outputs);
	PVAR->umon_setting_val.outputs =
	    (umon_setting_output_t *) malloc(PVAR->num_out_pp *
					     sizeof
					     (umon_setting_output_t));

	for (i = 0; i < PVAR->num_out_pp; ++i) {
		group = config_setting_get_elem(mon_group, i);
		// printf("Checking group %d\n",group);
		// Check if output is disabled

		res_group = config_setting_lookup(group, "resolution");
		pos_group = config_setting_lookup(group, "pos");
		config_setting_lookup_string(group, "EDID",
					     &(PVAR->
					       umon_setting_val.outputs[i].
					       edid_val));
		if (!config_setting_lookup_int
		    (group, "primary",
		     &(PVAR->umon_setting_val.outputs[i].is_primary))) {
			PVAR->umon_setting_val.outputs[i].is_primary = 0;
		}
		config_setting_lookup_int(res_group, "x",
					  &(PVAR->
					    umon_setting_val.outputs[i].
					    res_x));
		config_setting_lookup_int(res_group, "y",
					  &(PVAR->
					    umon_setting_val.outputs[i].
					    res_y));
		config_setting_lookup_int(pos_group, "x",
					  &(PVAR->
					    umon_setting_val.outputs[i].
					    pos_x));
		config_setting_lookup_int(pos_group, "y",
					  &(PVAR->
					    umon_setting_val.outputs[i].
					    pos_y));

	}

	group = config_setting_lookup(profile_group, "Screen");
	config_setting_lookup_int(group, "width",
				  &(PVAR->umon_setting_val.screen.width));
	config_setting_lookup_int(group, "height",
				  &(PVAR->umon_setting_val.screen.height));
	config_setting_lookup_int(group, "widthMM",
				  &(PVAR->umon_setting_val.
				    screen.widthMM));
	config_setting_lookup_int(group, "heightMM",
				  &(PVAR->umon_setting_val.
				    screen.heightMM));

	umon_print("Done loading values from configuration file\n");


}
