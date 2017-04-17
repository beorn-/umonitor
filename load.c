#include "load.h"

void load_class_constructor(load_class *self,
  screen_class *screen_t,config_t *config){

  self->load_profile = load_profile;
  self->screen_t_p = screen_t;
  self->config = config;
}


static void load_profile(load_class *self,config_setting_t *profile_group){

	int i;

	config_setting_t *pos_group,*group,*mon_group,*res_group;
	xcb_randr_crtc_t *crtcs_p;
	xcb_randr_set_crtc_config_cookie_t *crtc_config_p;
	xcb_randr_set_crtc_config_reply_t **crtc_config_reply_pp;
  xcb_randr_get_screen_resources_cookie_t screen_resources_cookie;
  xcb_randr_get_screen_resources_reply_t *screen_resources_reply;

	mon_group = config_setting_lookup(profile_group,"Monitors");
	// printf("Checking group %d\n",mon_group);
	self->num_out_pp = config_setting_length(mon_group);
	self->umon_setting_val.edid_val = (const char **) malloc(self->num_out_pp * sizeof(const char *));
	self->umon_setting_val.res_x = (int *) malloc(self->num_out_pp * sizeof(int));
	self->umon_setting_val.res_y = (int *) malloc(self->num_out_pp * sizeof(int));
	self->umon_setting_val.pos_x = (int *) malloc(self->num_out_pp * sizeof(int));
	self->umon_setting_val.pos_y = (int *) malloc(self->num_out_pp * sizeof(int));
	self->umon_setting_val.width = (int *) malloc(self->num_out_pp * sizeof(int));
	self->umon_setting_val.height = (int *) malloc(self->num_out_pp * sizeof(int));
	self->umon_setting_val.widthMM = (int *) malloc(self->num_out_pp * sizeof(int));
	self->umon_setting_val.heightMM = (int *) malloc(self->num_out_pp * sizeof(int));

	for(i=0;i<self->num_out_pp;++i) {
		group = config_setting_get_elem(mon_group,i);
		// printf("Checking group %d\n",group);
		res_group = config_setting_lookup(group,"resolution");
		pos_group = config_setting_lookup(group,"pos");
		config_setting_lookup_string(group,"EDID",
      self->umon_setting_val.edid_val+i);
		config_setting_lookup_int(res_group,"x",self->umon_setting_val.res_x+i);
		config_setting_lookup_int(res_group,"y",self->umon_setting_val.res_y+i);
		config_setting_lookup_int(pos_group,"x",self->umon_setting_val.pos_x+i);
		config_setting_lookup_int(pos_group,"y",self->umon_setting_val.pos_y+i);
		// printf("Loaded values: \n");
		// printf("EDID: %s\n",*(mySett->edid_val+i));
		// printf("Resolution: %s\n",*(mySett->resolution_str+i));
		// printf("Pos: x=%d y=%d\n",*(mySett->pos_val+2*i),*(mySett->pos_val+2*i+1));
	}

	group = config_setting_lookup(profile_group,"Screen");
	config_setting_lookup_int(group,"width",self->umon_setting_val.width);
	config_setting_lookup_int(group,"height",self->umon_setting_val.height);
	config_setting_lookup_int(group,"widthMM",self->umon_setting_val.widthMM);
	config_setting_lookup_int(group,"heightMM",self->umon_setting_val.heightMM);

	if (VERBOSE) printf("Done loading values from configuration file\n");

/*
	 * Plan of attack
	 * 1. Disable all crtcs
	 * 2. Resize the screen
	 * 3. Enabled desired crtcs
*/

  screen_resources_cookie = xcb_randr_get_screen_resources(self->screen_t_p->c,
    self->screen_t_p->screen->root);

 	screen_resources_reply =
 		xcb_randr_get_screen_resources_reply(self->screen_t_p->c,
      screen_resources_cookie,self->screen_t_p->e);

	crtcs_p = xcb_randr_get_screen_resources_crtcs(screen_resources_reply);
	crtc_config_p = (xcb_randr_set_crtc_config_cookie_t *)
    malloc(
      screen_resources_reply->num_crtcs*sizeof(
      xcb_randr_set_crtc_config_cookie_t));

	for(i=0;i<screen_resources_reply->num_crtcs;++i){
    if (!VERBOSE) {
		crtc_config_p[i] = xcb_randr_set_crtc_config(self->screen_t_p->c,crtcs_p[i],
      XCB_CURRENT_TIME, screen_resources_reply->config_timestamp,0,0,XCB_NONE,
      XCB_RANDR_ROTATION_ROTATE_0,0,0);
    }
    else{
      printf("Would disable crtcs here\n");
    }
	}

	for(i=0;i<screen_resources_reply->num_crtcs;++i){
		if (!VERBOSE) {
      crtc_config_reply_pp[i] =
    xcb_randr_set_crtc_config_reply(self->screen_t_p->c,crtc_config_p[i],
      self->screen_t_p->e);
    }
    else {
      printf("Would disable crtcs here\n");
    }
	}

	if (!VERBOSE) {
    xcb_randr_set_screen_size(self->screen_t_p->c,
      self->screen_t_p->screen->root,*(self->umon_setting_val.width),
      *(self->umon_setting_val.height),*(self->umon_setting_val.widthMM),
      *(self->umon_setting_val.heightMM));
	}
  else{
    printf("Would change screen size here\n");
  }

	for_each_output((void *) self,screen_resources_reply,match_with_config);
}

static void match_with_config(void *self_void,xcb_randr_output_t *output_p){


	xcb_randr_get_output_info_cookie_t output_info_cookie;
  xcb_randr_get_output_property_cookie_t output_property_cookie;
  xcb_randr_get_output_property_reply_t *output_property_reply;
  load_class *self = (load_class *) self_void;

	char *edid_string;
	uint8_t *output_property_data;
	int output_property_length;
	uint8_t delete = 0;
	uint8_t pending = 0;

  self->cur_output = output_p;
	output_info_cookie =
	xcb_randr_get_output_info(self->screen_t_p->c,*output_p,XCB_CURRENT_TIME);

  // TODO Duplicate code, fetching edid info
	output_property_cookie = xcb_randr_get_output_property(self->screen_t_p->c,
    *output_p,self->screen_t_p->edid_atom->atom,AnyPropertyType,0,100,
    delete,pending);
	self->output_info_reply =
		xcb_randr_get_output_info_reply (self->screen_t_p->c,output_info_cookie,
      self->screen_t_p->e);
	output_property_reply = xcb_randr_get_output_property_reply(
		self->screen_t_p->c,output_property_cookie,self->screen_t_p->e);
	output_property_data = xcb_randr_get_output_property_data(
		output_property_reply);
	output_property_length = xcb_randr_get_output_property_data_length(
		output_property_reply);

	edid_to_string(output_property_data,output_property_length,
		&edid_string);

	for(self->conf_output_idx=0;self->conf_output_idx<self->num_out_pp;++self->conf_output_idx){
		if (!strcmp(self->umon_setting_val.edid_val[self->conf_output_idx],edid_string)){
			// Found a match between the configuration file edid and currently connected edid. Now have to load correct settings.
			// Need to find the proper crtc to assign the output
			// Which crtc has the same resolution?
			for_each_output_mode((void *) self,self->output_info_reply,
        find_crtc_match);
			//find_crtc_by_res(mySett.res_x,mySett.res_y);
			// Connect correct crtc to correct output

			//crtc_config_p = xcb_randr_set_crtc_config(c,crtcs_p[i],XCB_CURRENT_TIME, screen_resources_reply->config_timestamp,0,0,XCB_NONE,  XCB_RANDR_ROTATION_ROTATE_0,NULL,0);
      //if (VERBOSE) printf("I would disable crtcs here\n");



		}

	}



}

static void find_crtc_match(void *self_void,xcb_randr_mode_t *mode_id_p){


	xcb_randr_crtc_t *crtc_p;
  //xcb_randr_mode_t *crtc_mode;
  xcb_randr_get_crtc_info_cookie_t crtc_info_cookie;
  xcb_randr_get_crtc_info_reply_t *crtc_info_reply;
  xcb_randr_set_crtc_config_cookie_t set_crtc_cookie;
  xcb_randr_set_crtc_config_reply_t *set_crtc_reply;
	int i,num_crtcs;
  load_class *self = (load_class *) self_void;



	num_crtcs = xcb_randr_get_output_info_crtcs_length(self->output_info_reply);
	crtc_p = xcb_randr_get_output_info_crtcs(self->output_info_reply);


// TODO Can optimize server fetching times
	for(i=0;i<num_crtcs;++i){

    crtc_info_cookie =
     xcb_randr_get_crtc_info(self->screen_t_p->c,*crtc_p,XCB_CURRENT_TIME);
    crtc_info_reply =
      xcb_randr_get_crtc_info_reply(self->screen_t_p->c,crtc_info_cookie,
         self->screen_t_p->e);


    if (crtc_info_reply->mode==*mode_id_p){
      // Found the matching crtc
      if (VERBOSE){
        printf("I found the crtc\n");
      }
      else{

      set_crtc_cookie =
      xcb_randr_set_crtc_config_unchecked(self->screen_t_p->c,*crtc_p,
        XCB_CURRENT_TIME,XCB_CURRENT_TIME,
        self->umon_setting_val.pos_x[self->conf_output_idx],
        self->umon_setting_val.pos_x[self->conf_output_idx],
        crtc_info_reply->mode,0, 1, self->cur_output);
      }
    }
		++crtc_p;
	}

}
