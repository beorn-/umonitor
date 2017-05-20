#include "umonitor.h"

/*! \mainpage Main Page
 *
 * \section intro_sec Introduction
 *
 * This program is intended to manage monitor hotplugging.

 	 \section readme README
	 Usage:
	 				1. Setup your configuration using xrandr or related tools
	 				2. Run \code umonitor --save <profile_name> \endcode
					3. Run \code umonitor --listen \endcode to begin automatically detecting and changing monitor configuration
 *
 */

/*! \file
		\brief Main file

		Contains the main function plus some helper functions that are shared by the classes
*/

static const char help_str[]=
"Usage: umonitor [OPTION]\n"
"\n"
"Options:\n"
"\t--save <profile_name>\tSaves current setup into profile_name\n"
"\t--delete <profile_name>\tRemoves profile_name from configuration file\n"
"\t--load <profile_name>\tLoads setup from profile name\n"
"\t--listen\t\tListens for changes in the setup and applies the new"
" configuration automatically\n"
"\t--help\t\t\tDisplay this help and exit\n"
"\t--version\t\tOutput version information and exit\n"
;

static const char version_str[]=
"umonitor 20170518\n"
"Written by Ricky Liou\n"
;

void umon_print(const char *format, ...){
	va_list args;

	if (VERBOSE){
		va_start(args, format);
		vprintf(format, args);
		va_end(args);
	}

}

/*! Logic for parsing options here*/

int main(int argc, char **argv) {
	int save = 0;
	int load = 0;
	int delete = 0;
	int listen = 0;
	int help = 0;
	int version = 0;
	save_class *save_o;
	load_class *load_o;
	autoload_class *autoload_o;
	screen_class screen_o;

	config_t config;
	config_setting_t *root, *profile_group;
	char* profile_name;

	int i, cfg_idx;

	config_init(&config);

	for(i=1;i<argc;++i) {
		if (!strcmp("--save", argv[i])) {
			if (++i >= argc) {
								printf("Saving needs an argument!\n");
								exit(6);
						}
						profile_name = argv[i];
			save = 1;
		}
		else if (!strcmp("--load", argv[i])) {
			if (++i >= argc) {
								printf("Loading needs an argument!\n");
								exit(6);
						}
						profile_name = argv[i];
			load = 1;
		}
		else if (!strcmp("--delete", argv[i])){
						if (++i >= argc) {
								printf("Deleting needs an argument!\n");
								exit(6);
						}
			profile_name = argv[i];
			delete = 1;
		}
		else if (!strcmp("--verbose", argv[i])){
			VERBOSE = 1;
		}
		else if (!strcmp("--listen", argv[i])){
			listen = 1;
		}
		else if (!strcmp("--help", argv[i])){
			help = 1;
		}
		else if (!strcmp("--version", argv[i])){
			version = 1;
		}
		else {
			printf("Unrecognized argument: %s\n",argv[i]);
		}
	}

	if (save + load + listen >= 2) exit(10);

	screen_class_constructor(&screen_o);

	if(help){
		printf("%s",help_str);
		return 0;
	}
	if(version){
		printf("%s",version_str);
		return 0;
	}

	char *home_directory = getenv("HOME");
	//char *display_env = getenv("DISPLAY");
	//char *xauthority_env = getenv("XAUTHORITY");
	//printf("Display: %s\n",display_env);
	//printf("XAUTHORITY: %s\n",xauthority_env);
	//printf("Home directory: %s\n",home_directory);
	const char *conf_location = "/.config/umon2.conf";
	char *path = malloc((strlen(home_directory)+strlen(conf_location)));
	strcpy(path,home_directory);
	strcat(path,conf_location);
	//printf("Path: %s\n",path);
	CONFIG_FILE = path;
	//CONF_FP = fopen(path,"r");
	//if (!CONF_FP) printf("Cannot find configuration file\n");
	//printf("File pointer: %d\n",CONF_FP);
	// Existing config file to load setting values

	if (save || delete) {
		config_read_file(&config, CONFIG_FILE);
		profile_group = config_lookup(&config,profile_name);
		if (profile_group != NULL) {
			// Overwrite existing profile
			cfg_idx = config_setting_index(profile_group);
			root = config_root_setting(&config);
			config_setting_remove_elem(root,cfg_idx);
			umon_print("Deleted profile %s\n", profile_name);
		}
		if (save){
			umon_print("Saving current settings into profile: %s\n", profile_name);
			/*
			 * Always create the new profile group because above code has already
			 * deleted it if it existed before
			*/
			root = config_root_setting(&config);
			profile_group = config_setting_add(root,profile_name,CONFIG_TYPE_GROUP);

			save_class_constructor(&save_o,&screen_o,&config);
			save_o->save_profile(save_o,profile_group);
			save_class_destructor(save_o);
		}
	}


	if (load) {
		if(config_read_file(&config, CONFIG_FILE)){
			umon_print("Loading profile: %s\n", profile_name);
			// Load profile
			profile_group = config_lookup(&config,profile_name);

			if (profile_group != NULL) {
				load_class_constructor(&load_o,&screen_o);
				load_o->load_profile(load_o,profile_group);
				load_class_destructor(load_o);
			}
			else {
				printf("Profile %s not found\n",profile_name);
				exit(2);
			}
		}
		else{
			printf("No configuration file to load\n");
			exit(3);
		}
	}

	if (listen) {
	// TODO Will not use new configuration file if it is changed
		if(config_read_file(&config, CONFIG_FILE)){
			autoload_constructor(&autoload_o,&screen_o,&config);
			//autoload_o->find_profile_and_load(autoload_o);
			autoload_o->wait_for_event(autoload_o);
			umon_print("Autoloading\n");
			autoload_destructor(autoload_o);
		}
		else{
			printf("No configuration file to load\n");
			exit(3);
		}
	}

	// Free things
	// Screen destructor
	screen_class_destructor(&screen_o);

	config_destroy(&config);
	free(path);

}

/*! \brief Loop over each output

	Calls the callback function for each output
 */
void for_each_output(
	void *self,
	xcb_randr_get_screen_resources_reply_t *screen_resources_reply,
	void (*callback)(void *,xcb_randr_output_t *)){

		int i,outputs_length;

		xcb_randr_output_t *output_p;

		output_p = xcb_randr_get_screen_resources_outputs(screen_resources_reply);
		outputs_length =
			xcb_randr_get_screen_resources_outputs_length(screen_resources_reply);

		for (i=0; i<outputs_length; ++i){
			callback(self,output_p++);
		}

}

/*! \brief Loop over each output mode

Calls the callback function for each output mode

 */
void for_each_output_mode(
  void *self,
  xcb_randr_get_output_info_reply_t *output_info_reply,
	void (*callback)(void *,xcb_randr_mode_t *)){

	int j,num_output_modes;

	xcb_randr_mode_t *mode_id_p;

	num_output_modes =
		xcb_randr_get_output_info_modes_length(output_info_reply);
	//if (VERBOSE) printf("number of modes %d\n",num_output_modes);
	mode_id_p = xcb_randr_get_output_info_modes(output_info_reply);

	for (j=0;j<num_output_modes;++j){
		callback(self,mode_id_p++);
	}

}

/*! \brief Converts the edid that is returned from the X11 server into a string
 *
 * @param[in]		output_p		the output whose edid is desired
 * @param[in]		screen_t_p	the connection information
 * @param[out]	edid_string	the edid in string form
 */

void fetch_edid(xcb_randr_output_t *output_p,	screen_class *screen_t_p,
	 char **edid_string){

	int i,j;
	uint8_t delete = 0;
	uint8_t pending = 0;
	xcb_randr_get_output_property_cookie_t output_property_cookie;
	xcb_randr_get_output_property_reply_t *output_property_reply;
	uint8_t *edid;
	//char *edid_info;

	char vendor[4];
	// uint16_t product;
	//uint32_t serial;
	char modelname[13];

	output_property_cookie = xcb_randr_get_output_property(screen_t_p->c,
		*output_p,screen_t_p->edid_atom->atom,AnyPropertyType,0,100,
    delete,pending);

	output_property_reply = xcb_randr_get_output_property_reply(
		screen_t_p->c,output_property_cookie,&screen_t_p->e);

	edid = xcb_randr_get_output_property_data(
		output_property_reply);
	//length = xcb_randr_get_output_property_data_length(
		//output_property_reply);

	umon_print("Starting edid_to_string\n");
	// *edid_string = (char *) malloc((length+1)*sizeof(char));
	// for (z=0;z<length;++z) {
	// 	if ((char) edid[z] == '\0') {
	// 		*(*edid_string+z) = '0';
	// 	}
	// 	else {
	// 		*(*edid_string+z) = (char) edid[z];
	// 	}
	//
	// }
	// *(*edid_string+z) = '\0';

	*edid_string = (char *) malloc(17*sizeof(char));
	char sc = 'A'-1;
	vendor[0] = sc + (edid[8] >> 2);
	vendor[1] = sc + (((edid[8] & 0x03) << 3) | (edid[9] >> 5));
	vendor[2] = sc + (edid[9] & 0x1F);
	vendor[3] = '\0';

	//product = (edid[11] << 8) | edid[10];
	// serial = edid[15] << 24 | edid[14] << 16 | edid[13] << 8 | edid[12];
	// edid_info = malloc(length*sizeof(char));
	// snprintf(edid_info, length, "%04X%04X%08X", vendor, product, serial);

	for (i = 0x36; i < 0x7E; i += 0x12) { //read through descriptor blocks...
		if (edid[i] == 0x00) { //not a timing descriptor
			if (edid[i+3] == 0xfc) { //Model Name tag
				for (j = 0; j < 13; j++) {
					if (edid[i+5+j] == 0x0a)
						modelname[j] = 0x00;
					else
						modelname[j] = edid[i+5+j];
				}
			}
		}
	}

	printf("vendor: %s\n",vendor);
	printf("modelname: %s\n",modelname);
	snprintf(*edid_string,17,"%s %s",vendor,modelname);

	free(output_property_reply);
	umon_print("Finished edid_to_string\n");
}