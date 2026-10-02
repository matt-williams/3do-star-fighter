
/* Include Headers */

#include "draw_frame.h"
#include "SF_Io.h"
#include "Smoke_Control.h"
#include "Rotate_Land.h"
#include "Draw_Land.h"
#include "Plot_Graphic.h"
#include "Laser_Control.h"
#include "Bit_Control.h"
#include "SF_ArmLink.h"
#include "Explosion.h"
#include "Weapons.h"
#include "Collision.h"
#include "Maths_Stuff.h"
#include "Misc_Struct.h"
#include "Ground_Control.h"
#include "Bonus_Control.h"
#include "SF_Control.h"
#include "Stdio.h"
#include "SF_War.h"
#include "SF_ArmUtils.h"
#include "Sound_Control.h"
#include "Graphic_Struct.h"

#if defined(SF_WEB_PORT)
#include "../WebPort/sf_web_fixed_step.h"
#include "../WebPort/sf_web_simulation.h"
#define SF_SIMULATION_DELTA(value) sf_web_fixed_step_scale_legacy_delta(value)
#else
#define SF_SIMULATION_DELTA(value) (value)
#endif

long test_cam = 0 ;

static long engine_zoom = 0 ;
static long last_camera = 0 ;
static long dead_cam_rot = 0 ;
static long previous_camera_x_position ;
static long previous_camera_y_position ;
static long previous_camera_z_position ;
#if defined(SF_WEB_PORT)
static long camera_velocity_state_initialized;
#endif

void draw_frame_update_camera_state( long camera_number )
{
long temp_long ;
long coll_check ;
ship_stack *view_ship = camera[camera_number].view_ship ;
target_struct tracking_camera ;
rotate_node node_data ;
graphics_details *details = (graphics_details*) ships_adr ;

long ship_size = ( (1<<22) + ((details+view_ship->type)->clip_size) )>>12 ;

#if defined(SF_WEB_PORT)
if (camera_velocity_state_initialized == 0)
{
	previous_camera_x_position = camera_x_position ;
	previous_camera_y_position = camera_y_position ;
	previous_camera_z_position = camera_z_position ;
	camera_velocity_state_initialized = 1;
}
#else
previous_camera_x_position = camera_x_position ;
previous_camera_y_position = camera_y_position ;
previous_camera_z_position = camera_z_position ;
#endif

long docked_camera = 0 ;

//camera[camera_number].counter2 = 0 ;

if (	((view_ship->type)>>4) == PLAYERS_SHIP &&
		docked.status != DOCKING_OUT )
{
	docked_camera = 1 ;
	view_ship = docked.ship ;
}

// Test disable the camera
if (test_mode == 1)
{
	docked_camera = 0 ;
	camera[camera_number].type = -1 ;
}

// Is it camera type 0 - normal spin round ship

if ( (camera[camera_number].type) == CAMERA_NORMAL && docked_camera == 0 && camera[camera_number].counter2 == 0 )
{

	// Get the ships y rotation between positive and negative 512*1024 (+/- 180 degrees)
	// and multiply the camera x rotation cosine to make side view level


	//add on the camera y rot and limit the y rotation
	camera_y_rotation = ( (view_ship->y_rot) + (camera[camera_number].y_rot) )&ROT_LIMIT ;

	if ( camera_y_rotation >512*1024 ) camera_y_rotation -= 1024*1024 ;

	camera_y_rotation = ((camera_y_rotation *
						sf_cos_q12( ( (camera[camera_number].x_rot)&ROT_LIMIT )>>10 ))>>12)
						&ROT_LIMIT ;

	//Setup the x,y,z distances from the ship being viewed
	// Defualt pos can be overwritten if its a big ship or weapon

	//Move the camera to the left or right based on climb / dive when banked over
	node_data.x_pos = ( (view_ship->y_control)*( sf_sin_q12( (view_ship->z_rot)>>10 ) ) )>>1;

	//Move the camera back from the ship based on the zoom and duration thrust is used
	node_data.y_pos =  -( ( (camera[camera_number].zoom * ship_size )>>10 )+engine_zoom)<<12 ;

	//Move the camera up/down based on climb / dive when banked flat
	//Plus move the camera above the ship based on zoom and cosine camera y
	node_data.z_pos =  ( (( (camera[camera_number].zoom * ship_size)>>10 )>>2) *
						( sf_cos_q12( ((camera[camera_number].y_rot)&ROT_LIMIT)>>10 ) )) +
						(( (view_ship->y_control)*( sf_cos_q12( (view_ship->z_rot)>>10 ) ) )>>3) ;

	if ( (view_ship->type>>4) == BIG_SHIP )
	{
		//Move the camera to the left or right based on climb / dive when banked over
		node_data.x_pos = ( (view_ship->y_control)*( sf_sin_q12( (view_ship->z_rot)>>10 ) ) )>>2;

		//Move the camera back from the ship based on the zoom and duration thrust is used
		node_data.y_pos =  -( ( (camera[camera_number].zoom * ship_size )>>10 )+engine_zoom)<<12 ;

		//Move the camera up/down based on climb / dive when banked flat
		//Plus move the camera above the ship based on zoom and cosine camera y
		node_data.z_pos =  ( (( (camera[camera_number].zoom * ship_size)>>10 )>>2) *
							( sf_cos_q12( ((camera[camera_number].y_rot)&ROT_LIMIT)>>10 ) )) +
							(( (view_ship->y_control)*( sf_cos_q12( (view_ship->z_rot)>>10 ) ) )>>4) ;
	}

	if ( (view_ship->type>>4) == WEAPON )
	{
		//Move the camera to the left or right based on climb / dive when banked over
		node_data.x_pos = 0 ;

		//Move the camera back from the ship based on the zoom and duration thrust is used
		node_data.y_pos =  -( ( (camera[camera_number].zoom * ship_size )>>10 )+engine_zoom)<<12 ;

		//Move the camera up/down based on climb / dive when banked flat
		//Plus move the camera above the ship based on zoom and cosine camera y
		node_data.z_pos =  ( (( (camera[camera_number].zoom * ship_size)>>10 )>>2) *
							( sf_cos_q12( ((camera[camera_number].y_rot)&ROT_LIMIT)>>10 ) )) ;
	}



	camera_x_rotation = ((512+((camera[camera_number].x_rot)>>10)+((view_ship->x_rot)>>10))&1023) ;
	camera_y_rotation = camera_y_rotation>>10 ;

	// Limit the rate of change of x rot when on external cam to avoid captain flipper
	if ( ship_viewed_last_frame == view_ship )
	{
		temp_long = camera_x_rotation - ship_viewed_last_frame_x_rot ;

		if (temp_long > 512) temp_long -= 1024 ;
		if (temp_long < -512) temp_long += 1024 ;

		if ( temp_long > 32 )
		{
			temp_long -= ((temp_long-32)>>1) ;
			if ( temp_long > 48 ) temp_long -= ((temp_long-48)>>1) ;
			if ( temp_long > 64 ) temp_long = 64 ;
		}

		if ( temp_long < -32 )
		{
			temp_long -= ((temp_long+32)>>1) ;
			if ( temp_long < -48 ) temp_long -= ((temp_long+48)>>1) ;
			if ( temp_long < -64 ) temp_long = -64 ;
		}

		camera_x_rotation = (ship_viewed_last_frame_x_rot + temp_long)&1023 ;
	}
	ship_viewed_last_frame_x_rot = camera_x_rotation ;


	//Setup the x,y rot around the ship being viewed
	//node_data.x_rot = ((view_ship->x_rot)+(camera[camera_number].x_rot))&ROT_LIMIT ;

	node_data.x_rot = ((camera_x_rotation-512)<<10)&ROT_LIMIT ;

	node_data.y_rot = camera_y_rotation<<10 ;
	node_data.z_rot = 0 ;

	rotate_node_from_c ( &node_data ) ;

	camera_x_position = (view_ship->x_pos) + ((node_data.x_pos)) ;
	camera_y_position = (view_ship->y_pos) + ((node_data.y_pos)) ;
	camera_z_position = (view_ship->z_pos) + ((node_data.z_pos)) ;


	// Nice rolly the horizon effect for these types of ship - else keep it flat
	if (	(view_ship->type>>4) == BIG_SHIP ||
			(view_ship->type>>4) == SMALL_SHIP ||
			(view_ship->type>>4) == PLAYERS_SHIP )
	{
		camera_z_rotation = ( sf_sin_q12( (view_ship->z_rot)>>10 )
							* sf_cos_q12( ( (camera[camera_number].x_rot)&ROT_LIMIT )>>10 )
							>>19 )&1023 ;
	}
	else
	{
		camera_z_rotation = 0 ;
	}

	ship_viewed_last_frame = view_ship ;

}


// Is it camera type 1+ - tracking camera / fly by
if ( 	(camera[camera_number].type)==CAMERA_TRACKING ||
		(camera[camera_number].type)==CAMERA_FLYBY ||
		docked_camera != 0 ||
		camera[camera_number].counter2 > 0 )
{

	ship_viewed_last_frame = NULL ;


	// Is it a fixed tracking camera
	if ( (camera[camera_number].type)==CAMERA_TRACKING || camera[camera_number].counter2 > 0 )
	{
		// Is it a big ship
		if ( (view_ship->type)>>4 == BIG_SHIP && camera[camera_number].counter2 == 0 )
		{
			// Base camera position on ship pos with bottom bits cleared
			camera_x_position = (((view_ship->x_pos)>>30)<<30)+(1<<29) ;
			camera_y_position = (((view_ship->y_pos)>>30)<<30)+(1<<29) ;
			camera_z_position = (((view_ship->z_pos)>>30)<<30)+(1<<29) ;
		}
		else
		{
			// Base camera position on ship pos with bottom bits cleared
			camera_x_position = (((view_ship->x_pos)>>28)<<28)+(1<<27) ;
			camera_y_position = (((view_ship->y_pos)>>28)<<28)+(1<<27) ;
			camera_z_position = (((view_ship->z_pos)>>28)<<28)+(1<<27) ;
		}
	}


	// Has the camera just been switched to fly by - if so then define position
	// Or is the camera counter less than 0
	if ( (camera[camera_number].type)==CAMERA_FLYBY && camera[camera_number].counter2 == 0 )
	{
		if ( 	(camera[camera_number].type)!=last_camera ||
				camera[camera_number].counter<0 )
		{
			if ( ((view_ship->type)>>4) == BIG_SHIP )
			{
				// Flyby cam for big ships

				temp_long = (( ( (arm_random()) &127) +128)<<18) ;
				if ( (arm_random()&1) == 0 ) temp_long = -temp_long ;
				camera_x_position = (view_ship->x_pos)+((view_ship->x_vel)<<6)+temp_long ;

				temp_long = (( ( (arm_random()) &127) +128)<<18) ;
				if ( (arm_random()&1) == 0 ) temp_long = -temp_long ;
				camera_y_position = (view_ship->y_pos)+((view_ship->y_vel)<<6)+temp_long ;

				temp_long = (( ( (arm_random()) &127) +128)<<18) ;
				if ( (arm_random()&1) == 0 ) temp_long = -temp_long ;
				camera_z_position = (view_ship->z_pos)+((view_ship->z_vel)<<6)+temp_long ;
				camera[camera_number].counter = 128 ;
			}
			else
			{
				// Flyby cam for small ships
				temp_long = (( ( (arm_random()) &127) +128)<<16) ;
				if ( (arm_random()&1) == 0 ) temp_long = -temp_long ;
				camera_x_position = (view_ship->x_pos)+((view_ship->x_vel)<<4)+temp_long ;

				temp_long = (( ( (arm_random()) &127) +128)<<16) ;
				if ( (arm_random()&1) == 0 ) temp_long = -temp_long ;
				camera_y_position = (view_ship->y_pos)+((view_ship->y_vel)<<4)+temp_long ;

				temp_long = (( ( (arm_random()) &127) +128)<<16) ;
				if ( (arm_random()&1) == 0 ) temp_long = -temp_long ;
				camera_z_position = (view_ship->z_pos)+((view_ship->z_vel)<<4)+temp_long ;
				camera[camera_number].counter = 32 ;
			}
#if defined(SF_WEB_PORT)
			/* Camera state now advances five times per legacy update, so
			   remember this selection immediately rather than re-rolling
			   the flyby location on each fixed substep. */
			last_camera = camera[camera_number].type;
#endif
		}
	}

	// Is it a internal docking bay view or wot ?
	if (	(camera[camera_number].type) != CAMERA_TRACKING &&
			(camera[camera_number].type) != CAMERA_FLYBY &&
			camera[camera_number].counter2 == 0 )
	{
		node_data.x_pos = 0 ;
		node_data.y_pos = ( (1<<23) + (1<<22) + (1<<21) ) ;// - (docked.counter<<21) ;
		node_data.z_pos = ((1<<22)+(1<<20)) ;
		node_data.x_rot = view_ship->x_rot ;
		node_data.y_rot = view_ship->y_rot ;
		node_data.z_rot = view_ship->z_rot ;
		rotate_node_from_c ( &node_data ) ;
		camera_x_position = view_ship->x_pos + node_data.x_pos ;
		camera_y_position = view_ship->y_pos + node_data.y_pos ;
		camera_z_position = view_ship->z_pos + node_data.z_pos ;
		view_ship = camera[camera_number].view_ship ;
	}

	if ( camera[camera_number].counter2 > 0 )
	{
		dead_cam_rot += SF_SIMULATION_DELTA(16) ;
		if (dead_cam_rot >= 1024) dead_cam_rot -= 1024 ;

		camera_x_position =  camera[camera_number].x_pos + (sf_cos_q12( dead_cam_rot )<<15) ;
		camera_y_position =  camera[camera_number].y_pos + (sf_sin_q12( dead_cam_rot )<<15) ;
		camera_z_position =  camera[camera_number].z_pos + (8<<24) ;
	}

	// Are we in space
	if (planet_info.space_mission != 1)
	{
		// Check that the camera is above the hill level if less than set to min height

		coll_check = find_ground_height( camera_x_position , camera_y_position ) ;
		if (camera_z_position < (coll_check+(1<<24)) ) camera_z_position = coll_check + (1<<24) ;
	}



	// Use the target finder to calculate the cameras x,y rotation
	// Note the x and y positions are flipped over compared to
	// normal as used in targeting missiles etc.
	tracking_camera.x_aim = camera_x_position ;
	tracking_camera.y_aim = camera_y_position ;
	tracking_camera.z_pos = camera_z_position ;

	if ( camera[camera_number].counter2 > 0 )
	{
		tracking_camera.x_pos = camera[camera_number].x_pos ;
		tracking_camera.y_pos = camera[camera_number].y_pos ;
		tracking_camera.z_aim = camera[camera_number].z_pos ;
	}
	else
	{
		tracking_camera.x_pos = view_ship->x_pos ;
		tracking_camera.y_pos = view_ship->y_pos ;
		tracking_camera.z_aim = view_ship->z_pos ;
	}

	target_finder( &tracking_camera );

	camera_x_rotation = (tracking_camera.x_rot)>>10 ;
	camera_y_rotation = (tracking_camera.y_rot)>>10 ;
	camera_z_rotation = 0 ;

}
else
{
	// Are we in space
	if (planet_info.space_mission != 1)
	{
		// Check that the camera is above the hill level if less than set to min height
		coll_check = find_ground_height( camera_x_position , camera_y_position ) ;

		if (camera_z_position < (coll_check+(1<<21)) )
		{
			camera_z_position = coll_check + (1<<21) ;

			tracking_camera.x_aim = camera_x_position ;
			tracking_camera.y_aim = camera_y_position ;
			tracking_camera.z_pos = camera_z_position ;
			tracking_camera.x_pos = view_ship->x_pos ;
			tracking_camera.y_pos = view_ship->y_pos ;
			tracking_camera.z_aim = view_ship->z_pos ;

			target_finder( &tracking_camera );

			camera_x_rotation = (tracking_camera.x_rot)>>10 ;
			camera_y_rotation = (tracking_camera.y_rot)>>10 ;
		}
	}
}


if ( (camera[camera_number].type) == CAMERA_INTERNAL && docked_camera == 0 && camera[camera_number].counter2 == 0 )
{
	node_data.x_pos = 0 ;
	node_data.y_pos = 0 ;
	node_data.z_pos = (1<<20) ;
	node_data.x_rot = view_ship->x_rot ;
	node_data.y_rot = view_ship->y_rot ;
	node_data.z_rot = view_ship->z_rot ;
	rotate_node_from_c ( &node_data ) ;
	camera_x_position = view_ship->x_pos + node_data.x_pos ;
	camera_y_position = view_ship->y_pos + node_data.y_pos ;
	camera_z_position = view_ship->z_pos + node_data.z_pos ;

	camera_x_rotation = (((view_ship->x_rot)+512*1024)&ROT_LIMIT)>>10 ;
	camera_y_rotation = (view_ship->y_rot)>>10 ;
	camera_z_rotation = (view_ship->z_rot)>>10 ;

	ship_being_viewed = view_ship ;

	ship_viewed_last_frame = NULL ;
}
else
{
	ship_being_viewed = 0 ;
}

engine_zoom += SF_SIMULATION_DELTA((view_ship->thrust_control)>>6) ;

engine_zoom += SF_SIMULATION_DELTA(-(engine_zoom>>4)) ;



if ( camera_z_position < 0 )
{
	land_sort_offset = (-(camera_z_position>>24)) + 4 ;
}
else
{
	land_sort_offset = (camera_z_position>>24) + 4 ;
}

#if defined(SF_WEB_PORT)
	sf_web_simulation_set_camera_pose(&camera[camera_number],
		camera_x_position, camera_y_position, camera_z_position,
		camera_x_rotation << 10, camera_y_rotation << 10,
		camera_z_rotation << 10);
	sf_web_simulation_project_camera_pose(&camera[camera_number],
		&camera_x_position, &camera_y_position, &camera_z_position,
		&camera_x_rotation, &camera_y_rotation, &camera_z_rotation);
#endif
}

void draw_frame_advance_wave_state(void)
{
draw_frame_advance_wave_state_by(16);
}

void draw_frame_advance_wave_state_by(long phase_delta)
{
wave_counter = (wave_counter+phase_delta)&1023 ;
wave_counter2 = (wave_counter2+phase_delta)&1023 ;
wave_counter3 = (wave_counter3+phase_delta)&1023 ;
}

void draw_frame_advance_visual_state(long camera_number)
{
#if !defined(SF_WEB_PORT)
draw_frame_advance_wave_state();
#endif

// Cycle the collision box atg/ata highlight thingy colour
collision_box_colour_adder -= 1;
if (collision_box_colour_adder<0) collision_box_colour_adder=7;

//keep a record of the last camera used to check for camera changes
last_camera = camera[camera_number].type ;
//update camera counter
camera[camera_number].counter -= 1 ;

camera_x_velocity = previous_camera_x_position - camera_x_position ;
camera_y_velocity = previous_camera_y_position - camera_y_position ;
camera_z_velocity = previous_camera_z_position - camera_z_position ;
#if defined(SF_WEB_PORT)
previous_camera_x_position = camera_x_position ;
previous_camera_y_position = camera_y_position ;
previous_camera_z_position = camera_z_position ;
#endif

// Update the engine sounds for the 2 near by ships
update_engine_sounds() ;

}

void draw_frame_render(void)
{
	if (planet_info.space_mission != 1)
		rotate_land();

	// Plot all landscape sections and all graphics.
	draw_land();
}

void draw_frame(long camera_number)
{
	draw_frame_update_camera_state(camera_number);
	draw_land_update_state();
	#if defined(SF_WEB_PORT)
		/*
		 * The attract-mode path does not run Star3000's authoritative mission
		 * tick, so it owns one visual wave phase advance per demo frame.
		 */
		draw_frame_advance_wave_state();
		draw_frame_advance_visual_state(camera_number);
		draw_frame_render();
#else
	draw_frame_render();
	draw_frame_advance_visual_state(camera_number);
#endif
}
