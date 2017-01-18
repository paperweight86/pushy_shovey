#pragma once

#define TAT_FLOAT3_NO_PAD

#include "types.h"
using namespace uti;

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// NOTES:
// 
//  -The shared memory variables will be updated once per graphics frame.
//
//  -Each variable comes with a UNIT, RANGE, and UNSET description where applicable.
//     UNITS - Is the numeric form which a variable is stored in (e.g. KPH, Celsius)
//     RANGE - Is the min-max ranges for a variable
//     UNSET - Is the initialised/default/invalid value, depending on the variables usage
//
//  -Constant/unchanging values are included in the data, such as 'maxRPM', 'fuelCapacity' - this is done to allow percentage calculations.
//
//  -Also included are 12 unique enumerated types, to be used against the mentioned flag/state variables
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

enum constants
{
	max_racers = 64,
	max_string_length = 64,
	pcars_header_version = 5,
};

enum tyre : unsigned int
{
	tyre_front_left = 0,
	tyre_front_right,
	tyre_rear_left,
	tyre_rear_right,
	
	tyre_max
};

enum game_state : unsigned int
{
	game_state_exited = 0,
	game_state_front_end,
	game_state_ingame_playing,
	game_state_ingame_paused,
	game_state_ingame_restarting,
	game_state_ingame_replay,
	game_state_front_end_replay,

	game_state_max
};

enum session_state : unsigned int
{
	session_state_invalid = 0,
	session_state_practice,
	session_state_test,
	session_state_qualify,
	session_state_formation_lap,
	session_state_race,
	session_state_time_attack,

	session_state_max
};

enum race_state : unsigned int
{
	race_state_invalid,
	race_state_not_started,
	race_state_racing,
	race_state_finished,
	race_state_disqualified,
	race_state_retired,
	race_state_dnf,
	
	race_state_max
};

enum track_sector : unsigned int
{
	track_sector_invalid = 0,
	track_sector_start,
	track_sector_sector1,
	track_sector_sector2,
	track_sector_finish,
	track_sector_stop,
	
	track_sector_max
};

enum race_flag : unsigned int
{
	race_flag_none = 0,       // not used - only for some query functions
	race_flag_green,          // end of danger zone, or race started
	race_flag_blue,           // faster car wants to overtake the participant
	race_flag_white,          // approaching a slow car
	race_flag_yellow,         // danger on the racing surface itself
	race_flag_double_yellow,  // danger that wholly or partly blocks the racing surface
	race_flag_black,          // participant disqualified
	race_flag_chequered,      

	race_flag_max
};

enum race_flag_reason : unsigned int
{
	race_flag_reason_none = 0,
	race_flag_reason_solo_crash,
	race_flag_reason_vehicle_crash,
	race_flag_reason_vehicle_obstruction,

	race_flag_reason_max
};

enum pit_mode : unsigned int
{
	pit_mode_none = 0,
	pit_mode_driving_into_pits,
	pit_mode_in_pit,
	pit_mode_driving_out_of_pits,
	pit_mode_in_garage,
	
	pit_mode_max
};

enum pit_schedule : unsigned int
{
	pit_schedule_none = 0,        // nothing scheduled
	pit_schedule_standard,        // used for standard pit sequence
	pit_schedule_drive_through,   // used for drive-through penalty
	pit_schedule_stop_go,         // used for stop-go penalty

	pit_schedule_max
};

enum car_flag : unsigned int
{
	car_flag_headlight = (1 << 0),
	car_flag_engine_active = (1 << 1),
	car_flag_engine_warning = (1 << 2),
	car_flag_speed_limiter = (1 << 3),
	car_flag_abs = (1 << 4),
	car_flag_handbrake = (1 << 5),
};

enum tyre_flag : unsigned int
{
	tyre_attached = (1 << 0),
	tyre_inflated = (1 << 1),
	tyre_is_on_ground = (1 << 2),
};

enum terrain : unsigned int
{
	terrain_road = 0,
	terrain_low_grip_road,
	terrain_bumpy_road1,
	terrain_bumpy_road2,
	terrain_bumpy_road3,
	terrain_marbles,
	terrain_grassy_berms,
	terrain_grass,
	terrain_gravel,
	terrain_bumpy_gravel,
	terrain_rumble_strips,
	terrain_drains,
	terrain_tyrewalls,
	terrain_cementwalls,
	terrain_guardrails,
	terrain_sand,
	terrain_bumpy_sand,
	terrain_dirt,
	terrain_bumpy_dirt,
	terrain_dirt_road,
	terrain_bumpy_dirt_road,
	terrain_pavement,
	terrain_dirt_bank,
	terrain_wood,
	terrain_dry_verge,
	terrain_exit_rumble_strips,
	terrain_grasscrete,
	terrain_long_grass,
	terrain_slope_grass,
	terrain_cobbles,
	terrain_sand_road,
	terrain_baked_clay,
	terrain_astroturf,
	terrain_snowhalf,
	terrain_snowfull,
	terrain_damaged_road1,
	terrain_train_track_road,
	terrain_bumpycobbles,
	terrain_aries_only,
	terrain_orion_only,
	terrain_b1rumbles,
	terrain_b2rumbles,
	terrain_rough_sand_medium,
	terrain_rough_sand_heavy,

	terrain_max
};

enum crash_damage : unsigned int
{
	crash_damage_none = 0,
	crash_damage_offtrack,
	crash_damage_large_prop,
	crash_damage_spinning,
	crash_damage_rolling,
	
	crash_max
};

struct racer
{
	bool m_is_active;
	char m_name[max_string_length];                   // [ string ]
	float3 m_world_position;                   // [ UNITS = World Space  X  Y  Z ]
	float m_current_lap_distance;                       // [ UNITS = Metres ]   [ RANGE = 0.0f->... ]    [ UNSET = 0.0f ]
	unsigned int m_race_position;                      // [ RANGE = 1->... ]   [ UNSET = 0 ]
	unsigned int m_laps_completed;                     // [ RANGE = 0->... ]   [ UNSET = 0 ]
	unsigned int m_current_lap;                        // [ RANGE = 0->... ]   [ UNSET = 0 ]
	track_sector m_current_sector;                     // [ enum (Type#4) Current Sector ]
};

struct pcars_memory_frame
{
	// Version Number
	unsigned int m_version;                           // [ RANGE = 0->... ]
	unsigned int m_build_version_number;                // [ RANGE = 0->... ]   [ UNSET = 0 ]

	game_state m_game_state;                     
	session_state m_session_state;               
	race_state m_race_state;                      

	int m_viewed_racer_index;   // [ RANGE = 0->max_racers ]   [ UNSET = -1 ]
	int m_num_racers;			// [ RANGE = 0->max_racers ]   [ UNSET = -1 ]
	racer racers[max_racers];

													  // Unfiltered Input
	float m_unfiltered_throttle;                        // [ RANGE = 0.0f->1.0f ]
	float m_unfiltered_brake;                           // [ RANGE = 0.0f->1.0f ]
	float m_unfiltered_steering;                        // [ RANGE = -1.0f->1.0f ]
	float m_unfiltered_clutch;                          // [ RANGE = 0.0f->1.0f ]
													 
	char m_car_name[max_string_length];              
	char m_car_class_name[max_string_length];        

	unsigned int m_laps_in_event;                        // [ RANGE = 0->... ]   [ UNSET = 0 ]
	char m_track_location[max_string_length]; 
	char m_track_variation[max_string_length];
	float m_track_length;                               // [ UNITS = Metres ]   [ RANGE = 0.0f->... ]    [ UNSET = 0.0f ]

	bool m_lap_invalidated;                             // [ UNITS = boolean ]   [ RANGE = false->true ]   [ UNSET = false ]
	float m_best_lap_time;                               // [ UNITS = seconds ]   [ RANGE = 0.0f->... ]   [ UNSET = -1.0f ]
	float m_last_lap_time;                               // [ UNITS = seconds ]   [ RANGE = 0.0f->... ]   [ UNSET = 0.0f ]
	float m_currentTime;                               // [ UNITS = seconds ]   [ RANGE = 0.0f->... ]   [ UNSET = 0.0f ]
	float m_splitTimeAhead;                            // [ UNITS = seconds ]   [ RANGE = 0.0f->... ]   [ UNSET = -1.0f ]
	float m_splitTimeBehind;                           // [ UNITS = seconds ]   [ RANGE = 0.0f->... ]   [ UNSET = -1.0f ]
	float m_splitTime;                                 // [ UNITS = seconds ]   [ RANGE = 0.0f->... ]   [ UNSET = 0.0f ]
	float m_eventTimeRemaining;                        // [ UNITS = milli-seconds ]   [ RANGE = 0.0f->... ]   [ UNSET = -1.0f ]
	float m_personal_fastest_lap_time;                    // [ UNITS = seconds ]   [ RANGE = 0.0f->... ]   [ UNSET = -1.0f ]
	float m_world_fastest_lap_time;                       // [ UNITS = seconds ]   [ RANGE = 0.0f->... ]   [ UNSET = -1.0f ]
	float m_current_sector_1_Time;                        // [ UNITS = seconds ]   [ RANGE = 0.0f->... ]   [ UNSET = -1.0f ]
	float m_current_sector_2_Time;                        // [ UNITS = seconds ]   [ RANGE = 0.0f->... ]   [ UNSET = -1.0f ]
	float m_current_sector_3_Time;                        // [ UNITS = seconds ]   [ RANGE = 0.0f->... ]   [ UNSET = -1.0f ]
	float m_fastest_sector_1_Time;                        // [ UNITS = seconds ]   [ RANGE = 0.0f->... ]   [ UNSET = -1.0f ]
	float m_fastest_sector_2_Time;                        // [ UNITS = seconds ]   [ RANGE = 0.0f->... ]   [ UNSET = -1.0f ]
	float m_fastest_sector_3_time;                        // [ UNITS = seconds ]   [ RANGE = 0.0f->... ]   [ UNSET = -1.0f ]
	float m_personal_fastest_sector_1_time;                // [ UNITS = seconds ]   [ RANGE = 0.0f->... ]   [ UNSET = -1.0f ]
	float m_personal_fastest_sector_2_time;                // [ UNITS = seconds ]   [ RANGE = 0.0f->... ]   [ UNSET = -1.0f ]
	float m_personal_fastest_sector_3_time;                // [ UNITS = seconds ]   [ RANGE = 0.0f->... ]   [ UNSET = -1.0f ]
	float m_world_fastest_sector_1_time;                   // [ UNITS = seconds ]   [ RANGE = 0.0f->... ]   [ UNSET = -1.0f ]
	float m_world_fastest_sector_2_time;                   // [ UNITS = seconds ]   [ RANGE = 0.0f->... ]   [ UNSET = -1.0f ]
	float m_world_fastest_sector_3_time;                   // [ UNITS = seconds ]   [ RANGE = 0.0f->... ]   [ UNSET = -1.0f ]

	// Racing Flags
	race_flag		 m_highest_flag_colour;             
	race_flag_reason m_highest_flag_reason;
													 
	pit_mode m_pit_mode;                          
	pit_schedule m_pit_schedule;                  

	car_flag m_car_flags;							 
	float m_oil_temp_celsius;                 // [ UNITS = Celsius ]   [ UNSET = 0.0f ]
	float m_oil_pressure_kpa;                 // [ UNITS = Kilopascal ]   [ RANGE = 0.0f->... ]   [ UNSET = 0.0f ]
	float m_water_temp_celsius;               // [ UNITS = Celsius ]   [ UNSET = 0.0f ]
	float m_water_pressure_kpa;               // [ UNITS = Kilopascal ]   [ RANGE = 0.0f->... ]   [ UNSET = 0.0f ]
	float m_fuel_pressure_kpa;                // [ UNITS = Kilopascal ]   [ RANGE = 0.0f->... ]   [ UNSET = 0.0f ]
	float m_fuel_level;                      // [ RANGE = 0.0f->1.0f ]
	float m_fuel_capacity;                   // [ UNITS = Liters ]   [ RANGE = 0.0f->1.0f ]   [ UNSET = 0.0f ]
	float m_speed;                          // [ UNITS = Metres per-second ]   [ RANGE = 0.0f->... ]
	float m_rpm;                            // [ UNITS = Revolutions per minute ]   [ RANGE = 0.0f->... ]   [ UNSET = 0.0f ]
	float m_max_rpm;                         // [ UNITS = Revolutions per minute ]   [ RANGE = 0.0f->... ]   [ UNSET = 0.0f ]
	float m_brake;                          // [ RANGE = 0.0f->1.0f ]
	float m_throttle;                       // [ RANGE = 0.0f->1.0f ]
	float m_clutch;                         // [ RANGE = 0.0f->1.0f ]
	float m_steering;                       // [ RANGE = -1.0f->1.0f ]
	int m_gear;                             // [ RANGE = -1 (Reverse)  0 (Neutral)  1 (Gear 1)  2 (Gear 2)  etc... ]   [ UNSET = 0 (Neutral) ]
	int m_num_gears;                         // [ RANGE = 0->... ]   [ UNSET = -1 ]
	float m_odometer_km;                     // [ RANGE = 0.0f->... ]   [ UNSET = -1.0f ]
	bool m_antilock_active;                  // [ UNITS = boolean ]   [ RANGE = false->true ]   [ UNSET = false ]
	int m_last_opponent_collision_index;       // [ RANGE = 0->STORED_PARTICIPANTS_MAX ]   [ UNSET = -1 ]
	float m_last_opponent_collision_magnitude; // [ RANGE = 0.0f->... ]
	bool m_boost_active;                     // [ UNITS = boolean ]   [ RANGE = false->true ]   [ UNSET = false ]
	float m_boost_amount;                    // [ RANGE = 0.0f->100.0f ] 

	// Motion & Device Related
	float3 m_orientation;                     // [ UNITS = Euler Angles ]
	float3 m_local_velocity;                   // [ UNITS = Metres per-second ]
	float3 m_world_velocity;                   // [ UNITS = Metres per-second ]
	float3 m_angular_velocity;                 // [ UNITS = Radians per-second ]
	float3 m_local_acceleration;               // [ UNITS = Metres per-second ]
	float3 m_world_acceleration;               // [ UNITS = Metres per-second ]
	float3 m_extents_centre;                   // [ UNITS = Local Space  X  Y  Z ]

	tyre m_tyre_flags[tyre_max];               // [ enum (Type#10) Tyre Flags ]
	terrain m_terrain[tyre_max];                 // [ enum (Type#11) Terrain Materials ]
	float m_tyre_y[tyre_max];                          // [ UNITS = Local Space  Y ]
	float m_tyre_rps[tyre_max];                        // [ UNITS = Revolutions per second ]
	float m_tyre_slip_speed[tyre_max];                  // [ UNITS = Metres per-second ]
	float m_tyre_temp[tyre_max];                       // [ UNITS = Celsius ]   [ UNSET = 0.0f ]
	float m_tyre_grip[tyre_max];                       // [ RANGE = 0.0f->1.0f ]
	float m_tyre_height_above_ground[tyre_max];          // [ UNITS = Local Space  Y ]
	float m_tyre_lateral_stiffness[tyre_max];           // [ UNITS = Lateral stiffness coefficient used in tyre deformation ]
	float m_tyre_wear[tyre_max];                       // [ RANGE = 0.0f->1.0f ]
	float m_brake_damage[tyre_max];                    // [ RANGE = 0.0f->1.0f ]
	float m_suspension_damage[tyre_max];               // [ RANGE = 0.0f->1.0f ]
	float m_brake_temp_celsius[tyre_max];               // [ UNITS = Celsius ]
	float m_tyre_tread_temp[tyre_max];                  // [ UNITS = Kelvin ]
	float m_tyre_layer_temp[tyre_max];                  // [ UNITS = Kelvin ]
	float m_tyre_carcass_temp[tyre_max];                // [ UNITS = Kelvin ]
	float m_tyre_rim_temp[tyre_max];                    // [ UNITS = Kelvin ]
	float m_tyre_internal_air_temp[tyre_max];            // [ UNITS = Kelvin ]

													 // Car Damage
	crash_damage m_crash_state;                        // [ enum (Type#12) Crash Damage State ]
	float m_aero_damage;                               // [ RANGE = 0.0f->1.0f ]
	float m_engine_damage;                             // [ RANGE = 0.0f->1.0f ]

													 // Weather
	float  m_ambient_temperature;                       // [ UNITS = Celsius ]   [ UNSET = 25.0f ]
	float  m_track_temperature;                         // [ UNITS = Celsius ]   [ UNSET = 30.0f ]
	float  m_rain_density;                              // [ UNITS = How much rain will fall ]   [ RANGE = 0.0f->1.0f ]
	float  m_wind_speed;                                // [ RANGE = 0.0f->100.0f ]   [ UNSET = 2.0f ]
	float2 m_wind_direction;                           // [ UNITS = Normalised Vector X ]
	float  m_cloud_brightness;                          // [ RANGE = 0.0f->... ]
};
