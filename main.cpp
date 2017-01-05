
/*
TODO: 

General:
- Set a target frequency (static or dynamic) to hit and sleep for any spare time to save CPU cycles
- I think we're mis-using the mem map file https://msdn.microsoft.com/en-us/library/ms810613.aspx
- Read Midi file format and allow play back

MYSQL:
- Move requests to thread with a basic job system

Verbose File Saving:
- save file per game with: <date>_<mode>_<track>_<car>.chod
- masking off parts of the data - is that even possible?

Time Trial Reporting:
- Report split times

Race Reporting:
- Check if we're in a valid race
	- Race is in the database on a specific date
	- Race has track and car class and optionally car id
- Report positions of racers (or only self?)

Website:
- Race callendar
- Admin race callendar
- Race results
- Show split times on all racer times page

*/

#include "pcars\pcars_memory_frame.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <io.h>

#include <thread>
#include <atomic>

#include "mysql.h"

#define PROJECT_CARS_APP_ID_STR "234630"

#include <conio.h>
#include <mmsystem.h>

void preallocate_file_space(u64 size, const char* filename)
{
	printf("Allocating disk space...\n");
	FILE* file = nullptr;
	auto err = fopen_s(&file, filename, "wb+");
	HANDLE osfHandle = (HANDLE)_get_osfhandle(_fileno(file));
	FlushFileBuffers(osfHandle);
	DWORD high = size >> 32;
	DWORD low = (DWORD)size;
	HANDLE h = ::CreateFileMapping(osfHandle, 0, PAGE_READWRITE, high, low, 0);
	DWORD dwError;
	if (h == (HANDLE)NULL)
		dwError = GetLastError();
	LARGE_INTEGER tempLrgInt;
	tempLrgInt.QuadPart = size;
	SetFilePointerEx(h, tempLrgInt, 0, FILE_BEGIN);
	SetEndOfFile(h);
	tempLrgInt.QuadPart = 0;
	SetFilePointerEx(h, tempLrgInt, 0, FILE_BEGIN);
	fclose(file);
	CloseHandle(h);
}

bool is_in_game(pcars_memory_frame* frame)
{
	return frame->m_game_state == game_state_ingame_paused
		|| frame->m_game_state == game_state_ingame_playing
		|| frame->m_game_state == game_state_ingame_restarting;
}

enum reporter_status
{
	reporter_status_none,
	reporter_status_loading,
	reporter_status_waiting,
	reporter_status_reporting,
	reporter_status_fatal_error,
};

enum laptime_status
{
	laptime_status_valid,
	laptime_status_invalid,
};

struct reporter_info
{
	std::atomic<reporter_status>	status;
	std::atomic<laptime_status>		lap_status;
	std::atomic<bool>				reported_lap;
	std::atomic<bool>				reported_fastest_lap;
};

const char* GetReporterStatusString(reporter_status status)
{
	switch (status)
	{
	case reporter_status_loading:
		return "Connecting to database...";
	case reporter_status_waiting:
		return "Waiting for game to run...";
	case reporter_status_reporting:
		return "Ready.";
	case reporter_status_fatal_error:
		return "Fatal error - unable to continue";
	case reporter_status_none:
	default:
		return "UNKNOWN STATUS!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!111111oneone";
	}
}

LONG GetDWORDRegKey(HKEY hKey, const char* strValueName, DWORD* nValue, DWORD nDefaultValue)
{
	*nValue = nDefaultValue;
	DWORD dwBufferSize(sizeof(DWORD));
	DWORD nResult(0);
	LONG nError = ::RegQueryValueExA(hKey,
		strValueName,
		0,
		NULL,
		reinterpret_cast<LPBYTE>(&nResult),
		&dwBufferSize);
	if (ERROR_SUCCESS == nError)
	{
		*nValue = nResult;
	}
	return nError;
}

uti::u64 get_track_id(MYSQL* server, const char* name, const char* variant)
{
	const char* insert_test_query_format = "INSERT INTO `test`.`pcars_tracks` (`id`,`track_name`) VALUES (NULL, '%s: %s') ON DUPLICATE KEY UPDATE `track_name`=VALUES(`track_name`), id=LAST_INSERT_ID(id);";
	const int max_string = 256;
	char query[max_string] = {};
	//unsigned long escape_result = mysql_real_escape_string(server, nickname_escaped, steam_nickname, min(strlen(steam_nickname), max_string));
	sprintf_s<256>(query, insert_test_query_format, name, variant);
	int result = mysql_real_query(server, query, strlen(query));
	return mysql_insert_id(server);
}

uti::u64 get_car_id(MYSQL* server, const char* name, const char* car_class)
{
	const char* insert_test_query_format = "INSERT INTO `test`.`pcars_cars` (`id`,`car_name`,`car_class`) VALUES (NULL, '%s', '%s') ON DUPLICATE KEY UPDATE `car_name`=VALUES(`car_name`), id=LAST_INSERT_ID(id);";
	const int max_string = 256;
	char query[max_string] = {};
	sprintf_s<256>(query, insert_test_query_format, name, car_class);
	int result = mysql_real_query(server, query, strlen(query));
	return mysql_insert_id(server);
}

bool can_record_lap_time(pcars_memory_frame* frame)
{
	return frame->m_num_racers > 0
		&& frame->m_viewed_racer_index != -1
		&& frame->m_last_lap_time != -1.000
		&& is_in_game(frame)
		&& frame->m_session_state == session_state_time_attack;
}

bool can_record_race(pcars_memory_frame* frame)
{
	return frame->m_num_racers > 1
		&& is_in_game(frame)
		&& (    frame->m_session_state == session_state_race 
			 || frame->m_session_state == session_state_qualify );
}

bool can_record_race_file()
{
	// TODO:  Command line option
	return false;
}

void report_thread_func(reporter_info* info)
{
	info->status = reporter_status_loading;

	bool write_file = false;
	const char* username = "";
	const char* password = "";
	MYSQL pushydb = {};
	mysql_init(&pushydb);
	MYSQL* connect_result = mysql_real_connect(&pushydb, "192.168.0.8", username, password, "test", 0, 0, CLIENT_COMPRESS);
	
	// DAS DUUUUUUBLE BUFFER
	pcars_memory_frame frame1 = {};
	pcars_memory_frame frame2 = {};
	pcars_memory_frame* this_frame = &frame1;
	pcars_memory_frame* last_frame = &frame2;
	u64 frame_size = sizeof(pcars_memory_frame);
	
	HANDLE pcars_file_handle = OpenFileMapping(
		FILE_MAP_READ,
		FALSE,
		"$pcars$"
		);
	
wait:
	info->status = reporter_status_waiting;
	while (pcars_file_handle == NULL)
	{
		Sleep(1000);
		pcars_file_handle = OpenFileMapping(
			FILE_MAP_READ,
			FALSE,
			"$pcars$"
			);
	}

	info->status = reporter_status_reporting;
	HKEY hKey;
	LONG lRes = RegOpenKeyExA(HKEY_CURRENT_USER, "SOFTWARE\\Valve\\Steam\\ActiveProcess", 0, KEY_READ, &hKey);
	//bool bExistsAndSuccess(lRes == ERROR_SUCCESS);
	//bool bDoesNotExistsSpecifically(lRes == ERROR_FILE_NOT_FOUND);
	DWORD steam_id = 0;
	if (GetDWORDRegKey(hKey, "ActiveUser", &steam_id, 0) != 0 || steam_id == 0 )
	{
		printf("Error: Unable to determine active steam user.");
		info->status = reporter_status_fatal_error;
		return;
	}

	LARGE_INTEGER BeginTime, StartingTime, EndingTime, ElapsedMicroseconds, TotalMicroseconds;
	LARGE_INTEGER Frequency;
	
	QueryPerformanceFrequency(&Frequency);
	QueryPerformanceCounter(&StartingTime);
	QueryPerformanceCounter(&BeginTime);
	
	u64 total_written = 0;
	
	//if (pAddress != nullptr)
	{
		FILE* fp = nullptr;
		if (write_file)
		{
			const char* filename = "C:\\temp\\test.chod";
			preallocate_file_space(1024 * 1024 * 1024 /*1 gib*/, filename);
			fopen_s(&fp, filename, "wb+");
		}

		void* pAddress = MapViewOfFile(
			pcars_file_handle,
			FILE_MAP_READ,
			0,
			0,
			0
			);
		memcpy(this_frame, pAddress, sizeof(pcars_memory_frame));
		UnmapViewOfFile(pAddress);
		pAddress = nullptr;
		if (write_file)
		{
			fwrite(this_frame, sizeof(pcars_memory_frame), 1, fp);
			total_written += sizeof(pcars_memory_frame);
		}
		pcars_memory_frame* tmp = last_frame;
		last_frame = this_frame;
		this_frame = tmp;

		UnmapViewOfFile(pAddress);
		CloseHandle(pcars_file_handle);
		pcars_file_handle = NULL;

		const int max_string = 256;

		bool current_lap_time_valid = false;
		bool last_lap_time_valid = false;
		bool have_steam_nickname = false;

		while (	   info->status != reporter_status_fatal_error 
				&& info->status != reporter_status_none )
		{
			QueryPerformanceCounter(&EndingTime);
			ElapsedMicroseconds.QuadPart = EndingTime.QuadPart - StartingTime.QuadPart;

			ElapsedMicroseconds.QuadPart *= 1000000;
			ElapsedMicroseconds.QuadPart /= Frequency.QuadPart;

			Sleep(1000 / 10);

			// TODO:  For what we want we can probably sample way lower than we are - quater second?
			bool should_read = ElapsedMicroseconds.QuadPart / 1000 > 1000 / 120;
			bool could_read = true;
			if (should_read)
			{
				pcars_file_handle = OpenFileMapping(
					FILE_MAP_READ,
					FALSE,
					"$pcars$"
					);
				//printf("Writing file %I64u", ElapsedMicroseconds.QuadPart / 1000);
				pAddress = MapViewOfFile(
					pcars_file_handle,
					FILE_MAP_READ,
					0,
					0,
					0
					);
				if(pAddress == nullptr)
					could_read = false;
			}
			if(pAddress != nullptr)
			{
				memcpy(this_frame, pAddress, sizeof(pcars_memory_frame));
				UnmapViewOfFile(pAddress);
				CloseHandle(pcars_file_handle);
				pcars_file_handle = NULL;
				pAddress = nullptr;

				if (is_in_game(this_frame) && write_file)
				{
					fwrite(this_frame, sizeof(pcars_memory_frame), 1, fp);
					total_written += sizeof(pcars_memory_frame);
				}

				// left the game
				if (is_in_game(last_frame) && !is_in_game(this_frame))
				{
					printf("User left a game\n");
				}

				// entered a game
				if (!is_in_game(last_frame) && is_in_game(this_frame))
				{
					printf("User entered a game\n");
				}

				// Check we're in a valid time-attack state to record a lap time
				if (can_record_lap_time(this_frame))
				{
					// If we don't have the steam nickname we try to get it and update it
					if (!have_steam_nickname && is_in_game(this_frame) && last_frame->m_viewed_racer_index != -1)
					{
						// We have to guess that the user is the one being viewed when we enter the game...
						last_frame->m_viewed_racer_index != -1;
						char* steam_nickname = 0;
						char steam_id_buffer[max_string] = {};
						steam_nickname = last_frame->racers[last_frame->m_viewed_racer_index].m_name;

						char query[max_string] = {};
						const char* insert_test_query_format = "INSERT INTO `test`.`pcars_racers` (`id`,`steam_nickname`,`steam_id`) VALUES (NULL, '%s', '%d') ON DUPLICATE KEY UPDATE `steam_nickname`=VALUES(`steam_nickname`);";

						char nickname_escaped[max_string] = {};
						unsigned long escape_result = mysql_real_escape_string(connect_result, nickname_escaped, steam_nickname, min(strlen(steam_nickname), max_string));
						sprintf_s<256>(query, insert_test_query_format, nickname_escaped, steam_id);
						int result = mysql_real_query(connect_result, query, strlen(query));

						if (result != 0)
						{
							//info->status = reporter_status_fatal_error;
							printf("Error: unable to insert/update user");
						}
						else
						{
							have_steam_nickname = true;
						}
					}

					// Check to see if we've entered a new lap this frame
					if (this_frame->racers[this_frame->m_viewed_racer_index].m_current_lap > last_frame->racers[this_frame->m_viewed_racer_index].m_current_lap)
					{
						printf("Entering new lap #%d\n", this_frame->racers[this_frame->m_viewed_racer_index].m_current_lap);
						// If the previous lap was a valid one we can throw it in the database
						last_lap_time_valid = current_lap_time_valid;
						if (last_lap_time_valid)
						{
							printf("Last lap was valid - saving to db.\n");
							
							char query[max_string] = {};
							const char* insert_test_query_format = "INSERT INTO `test`.`pcars_lap_times` (`id`,`track_id`,`racer_id`, `car_id`, `lap_time`, `date_time`)"
																   "VALUES (NULL,		 '%d',		'%d',     '%d',		 '%f',		  NOW());";

							int track_id = get_track_id(connect_result, this_frame->m_track_location, this_frame->m_track_variation),
								car_id = get_car_id(connect_result, this_frame->m_car_name, this_frame->m_car_class_name);
							float lap_time = this_frame->m_last_lap_time;
							sprintf_s<256>(query, insert_test_query_format, track_id, steam_id, car_id, lap_time);
							int result = mysql_real_query(connect_result, query, strlen(query));
							if (result != 0)
							{
								// TODO:  Add a "job" to retry this in the future - handle all times like this probably
								printf("Error: Failed to save lap in db.\n");
								info->reported_lap = false;
								info->reported_fastest_lap = false;
							}
							else
							{
								printf("Time %f seconds for %s: %s saved in db.\n", lap_time, this_frame->m_track_location, this_frame->m_track_variation);
								info->reported_lap = true;
								info->reported_fastest_lap = this_frame->m_best_lap_time == this_frame->m_last_lap_time;
							}
						}
						else
						{
							printf("Invalid previous lap.\n");
						}
					}
				}				
				else if (can_record_race(this_frame))
				{
					// TODO:  We need to know if the race we're in is even a valid one but should do that async

				}

				current_lap_time_valid =   !this_frame->m_lap_invalidated
										&&	is_in_game(this_frame)
										&&	this_frame->m_session_state == session_state_time_attack
										//&&  this_frame->m_num_racers == 1
										&&  this_frame->m_viewed_racer_index != -1
										&&  this_frame->racers[this_frame->m_viewed_racer_index].m_current_sector == track_sector_sector2;

				// End of frame
				pcars_memory_frame* tmp = last_frame;
				last_frame = this_frame;
				this_frame = tmp;

				QueryPerformanceCounter(&StartingTime);
			}
			else if (should_read)
			{
				current_lap_time_valid = false;
				last_lap_time_valid = false;
			}
			else
			{
				YieldProcessor();
			}

			TotalMicroseconds.QuadPart = EndingTime.QuadPart - BeginTime.QuadPart;

			TotalMicroseconds.QuadPart *= 1000000;
			TotalMicroseconds.QuadPart /= Frequency.QuadPart;

			//if (TotalMicroseconds.QuadPart / 1000 / 1000 > 600)
			//{
			//	float data_per_sec = (float)total_written / 1024.0f / 1024.0f / 10.0f;
			//	printf("mib/s %f\r\n", data_per_sec);
			//	break;
			//}

			//lRes = RegOpenKeyExA(HKEY_CURRENT_USER, "SOFTWARE\\Valve\\Steam\\Apps\\" PROJECT_CARS_APP_ID_STR, 0, KEY_READ, &hKey);
			//DWORD is_pcars_running = 1;
			//todo: This reported falsely a few times that project cars was not running
			//if (GetDWORDRegKey(hKey, "Running", &is_pcars_running, 0) != 0)
			//{
			//	printf("Warning: Unable to determine if pcars is running.\r\n");
			//}
			//RegCloseKey(hKey);

			if (/*!is_pcars_running ||*/ (should_read && !could_read))
			{
				printf("Project cars has closed, ending reporting.\n");
				if (fp != NULL)
				{
					fclose(fp);
					fp = NULL;
				}
				pAddress = nullptr;
				CloseHandle(pcars_file_handle);
				pcars_file_handle = NULL;
				goto wait;
			}
		}
		if (fp != NULL)
		{
			fclose(fp);
		}
	}
}

char* midi_notes[128] = {
	"C_0", "CS0", "D_0", "DS0", "E_0", "F_0", "FS0", "G_0", "GS0", "A_0", "AS0", "B_0",
	"C_1", "CS1", "D_1", "DS1", "E_1", "F_1", "FS1", "G_1", "GS1", "A_1", "AS1", "B_1",
	"C_2", "CS2", "D_2", "DS2", "E_2", "F_2", "FS2", "G_2", "GS2", "A_2", "AS2", "B_2",
	"C_3", "CS3", "D_3", "DS3", "E_3", "F_3", "FS3", "G_3", "GS3", "A_3", "AS3", "B_3",
	"C_4", "CS4", "D_4", "DS4", "E_4", "F_4", "FS4", "G_4", "GS4", "A_4", "AS4", "B_4",
	"C_5", "CS5", "D_5", "DS5", "E_5", "F_5", "FS5", "G_5", "GS5", "A_5", "AS5", "B_5",
	"C_6", "CS6", "D_6", "DS6", "E_6", "F_6", "FS6", "G_6", "GS6", "A_6", "AS6", "B_6",
	"C_7", "CS7", "D_7", "DS7", "E_7", "F_7", "FS7", "G_7", "GS7", "A_7", "AS7", "B_7",
	"C_8", "CS8", "D_8", "DS8", "E_8", "F_8", "FS8", "G_8", "GS8", "A_8", "AS8", "B_8",
	"C_9", "CS9", "D_9", "DS9", "E_9", "F_9", "FS9", "G_9", "GS9", "A_9", "AS9", "B_9",
	"C_T", "CST", "D_T", "DST", "E_T", "F_T", "FST", "G_T" };

union midi_message { unsigned long word; unsigned char data[4]; };

const u32 num_midi_keys = 128;
bool midi_keys_on[num_midi_keys];

enum midi_msg_type : u8
{
	midi_msg_set_instrument = 0xC0,
	midi_msg_note_on		= 0x90,
	midi_msg_note_off		= 0x80,
};

void midi_make_key_on(midi_message* msg, i8 key, i8 volume, i8 channel = 0)
{
	msg->data[0] = midi_msg_note_on | channel;
	msg->data[1] = key;
	msg->data[2] = volume;
}

void midi_key_on(HMIDIOUT device, i8 key, i8 volume, i8 channel = 0)
{
	midi_message msg = {};
	midi_make_key_on(&msg, key, volume, channel);
	int flag = midiOutShortMsg(device, msg.word);
	if (flag != MMSYSERR_NOERROR) {
		printf("Error: could not set MIDI key %d on.\n", key);
	}
	midi_keys_on[key] = true;
}

void midi_make_key_off(midi_message* msg, i8 key, i8 channel = 0, i8 volume = 0)
{
	msg->data[0] = midi_msg_note_off | channel;
	msg->data[1] = key;
	msg->data[2] = volume;
}

void midi_key_off(HMIDIOUT device, i8 key, i8 channel = 0, i8 volume = 0)
{
	midi_message msg = {};
	midi_make_key_off(&msg, key, channel, volume);
	int flag = midiOutShortMsg(device, msg.word);
	if (flag != MMSYSERR_NOERROR) {
		printf("Error: could not set MIDI key %d on.\n", key);
	}
	midi_keys_on[key] = false;
}

void midi_make_set_instrument(midi_message* msg, i8 instrument, i8 channel = 0)
{
	msg->data[0] = midi_msg_set_instrument | channel;
	msg->data[1] = instrument;
}

void midi_set_instrument(HMIDIOUT device, i8 instrument, i8 channel = 0)
{
	midi_message msg = {};
	midi_make_set_instrument(&msg, instrument, channel);
	int flag = midiOutShortMsg(device, msg.word);
	if (flag != MMSYSERR_NOERROR) {
		printf("Error: could not set MIDI instrument to %d.\n", instrument);
	}
}

void midi_all_keys_off(HMIDIOUT device, i8 channel = 0)
{
	midi_message message = { (unsigned long)0 };
	message.data[0] = midi_msg_note_off | channel; // MIDI note-on message (requires to data bytes)
	for (u8 i = 0; i < num_midi_keys; ++i)
	{
		message.data[1] = i;
		int flag = midiOutShortMsg(device, message.word);
		if (flag != MMSYSERR_NOERROR) {
			printf("Error: could not set MIDI key %d off.\n", i);
		}
	}
}

struct midi_thread_data
{
	std::atomic<bool> should_close;
	reporter_info*	  reporter_info;
};

void music_thread_func(midi_thread_data* data)
{
	HMIDIOUT device;
	int midiport = 0;
	int flag = midiOutOpen(&device, midiport, 0, 0, CALLBACK_NULL);
	if (flag != MMSYSERR_NOERROR) 
	{
		printf("Error: could not open MIDI Output.\n");
	}

	midi_message message = { (unsigned long)0 };

	i8 channel = 1;
	i8 volume = 127;
	i8 instrument = 7;
	//for (u8 instrument = 0; instrument < 128; ++i)
	{
		printf("%d\r", instrument);
		midi_set_instrument(device, instrument, channel);
	
		midi_key_on(device, 35, volume, channel);
	
		Sleep(250);
		midi_all_keys_off(device, channel);
	
		midi_key_on(device, 25, volume, channel);
	
		Sleep(250);
		midi_all_keys_off(device, channel);
	
		midi_key_on(device, 45, volume, channel);
	
		Sleep(500);
		midi_all_keys_off(device, channel);
	
		midi_key_on(device, 35, volume, channel);
	
		Sleep(250);
		midi_all_keys_off(device, channel);
	
		midi_key_on(device, 25, volume, channel);
	
		Sleep(250);
		midi_all_keys_off(device, channel);
	
		midi_key_on(device, 40, volume, channel);
	
		Sleep(1000);
		midi_all_keys_off(device, channel);
	}


	channel = 1;
	volume = 127;
	instrument = 1;
	printf("%d\r", instrument);
	midi_set_instrument(device, instrument, channel);

	reporter_status last_status = reporter_status_none;
	while (!data->should_close)
	{
		if (last_status != data->reporter_info->status && data->reporter_info->status == reporter_status_reporting)
		{
			midi_key_on(device, 60, volume, channel);
			Sleep(100);
			midi_key_on(device, 60, volume, channel);
			Sleep(100);
			midi_all_keys_off(device, channel);
		}

		if (last_status != data->reporter_info->status && data->reporter_info->status == reporter_status_waiting)
		{
			midi_key_on(device, 80, volume, channel);
			Sleep(100);
			midi_key_on(device, 80, volume, channel);
			Sleep(100);
			midi_all_keys_off(device, channel);
		}
		
		if (data->reporter_info->reported_lap)
		{
			data->reporter_info->reported_lap = false;

			midi_key_on(device, 100, volume, channel);
			Sleep(100);
			midi_key_on(device, 110, volume, channel);
			Sleep(100);
			midi_key_on(device, 115, volume, channel);
			Sleep(100);
			if (data->reporter_info->reported_fastest_lap)
			{
				midi_key_on(device, 100, volume, channel);
				Sleep(200);
			}
			else
			{
				midi_key_on(device, 30, volume, channel);
				Sleep(200);
			}
			midi_all_keys_off(device, channel);
		}

		last_status = data->reporter_info->status;

		Sleep(100);
	}
}

void main()
{
	midi_thread_data midi_data = {};

	reporter_info reporter = {};

	midi_data.reporter_info = &reporter;

	std::thread midi_thread(music_thread_func, &midi_data);

	std::thread reporter_thread (report_thread_func, &reporter);

	const char* logo[] = {
"______ _   _ _____ _   ___   __      \r\n",
"| ___ | | | /  ___| | | \\ \\ / /      \r\n",
"| |_/ | | | \\ `--.| |_| |\\ V /       \r\n",
"|  __/| | | |`--. |  _  | \\ /        \r\n",
"| |   | |_| /\\__/ | | | | | |        \r\n",
"\\_|    \\___/\\____/\\_| |_/ \\_/        \r\n",
"                                     \r\n",
" _____ _   _ _____ _   _ _______   __\r\n",
"/  ___| | | |  _  | | | |  ___\\ \\ / /\r\n",
"\\ `--.| |_| | | | | | | | |__  \\ V / \r\n",
" `--. |  _  | | | | | | |  __|  \\ /  \r\n",
"/\\__/ | | | \\ \\_/ \\ \\_/ | |___  | |  \r\n",
"\\____/\\_| |_/\\___/ \\___/\\____/  \\_/  \r\n",
"                                     \r\n",
" _____ _     _   _______             \r\n",
"/  __ | |   | | | | ___ \\            \r\n",
"| /  \\| |   | | | | |_/ /            \r\n",
"| |   | |   | | | | ___ \\            \r\n",
"| \\__/| |___| |_| | |_/ /            \r\n",
" \\____\\_____/\\___/\\____/             \r\n" };
	//Sleep(5000);
	for (int i = 0; i < ARRAYSIZE(logo); ++i)
	{
		printf(logo[i]);
		Sleep(100);
	}
	printf("\r\n\r\n%s\r\n", GetReporterStatusString(reporter.status));

int anim_frame_size = 21;
const char* car[] =  {
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"                                                             \r\n",
"                                                             \r\n",
"                                                             \r\n",
"                                                             \r\n",
"                                                             \r\n",
"                                                             \r\n",
"                                                             \r\n",
"                                                             \r\n",
"                                                             \r\n",
"                                                             \r\n",
"                                                             \r\n",
"                                                             \r\n",
"                                                             \r\n",
"                                                             \r\n",
"                                                             \r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",

"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"            \r\n",
"            \r\n",
"            \r\n",
"            \r\n",
"            \r\n",
"            \r\n",
"|| []  []|\\\r\n",
"||##O#####] \r\n",
"|| []  []|/ \r\n",
"            \r\n",
"| []  []|\\ \r\n",
"|##O#####]  \r\n",
"| []  []|/  \r\n",
"            \r\n",
"            \r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",

"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"               \r\n",
"               \r\n",
"               \r\n",
"               \r\n",
"               \r\n",
"               \r\n",
" || []  []|\\ \r\n",
"s||##O#####]   \r\n",
" || []  []|/  \r\n",
"               \r\n",
"|| []  []|\\   \r\n",
"||##O#####]    \r\n",
"|| []  []|/    \r\n",
"               \r\n",
"               \r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",

"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"                \r\n",
"                \r\n",
"                \r\n",
"                \r\n",
"                \r\n",
"                \r\n",
"    || []  []|\\ \r\n",
"ings||##O#####] \r\n",
"    || []  []|/ \r\n",
"                \r\n",
"   || []  []|\\ \r\n",
"   ||##O#####]  \r\n",
"   || []  []|/  \r\n",
"                \r\n",
"                \r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",

"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"                    \r\n",
"                    \r\n",
"                    \r\n",
"                    \r\n",
"                    \r\n",
"                    \r\n",
"        || []  []|\\ \r\n",
"Jennings||##O#####] \r\n",
"        || []  []|/ \r\n",
"                    \r\n",
"      || []  []|\\  \r\n",
"      ||##O#####]   \r\n",
"      || []  []|/   \r\n",
"                    \r\n",
"                    \r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",

"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"                         \r\n",
"                         \r\n",
"                         \r\n",
"                         \r\n",
"                         \r\n",
"                         \r\n",
"             || []  []|\\ \r\n",
"niel Jennings||##O#####] \r\n",
"             || []  []|/ \r\n",
"                         \r\n",
"            || []  []|\\ \r\n",
"            ||##O#####]  \r\n",
"            || []  []|/  \r\n",
"                         \r\n",
"                         \r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",

"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"                             \r\n",
"                             \r\n",
"                             \r\n",
"                             \r\n",
"                             \r\n",
"                             \r\n",
"                 || []  []|\\ \r\n",
"y Daniel Jennings||##O#####] \r\n",
"                 || []  []|/ \r\n",
"                             \r\n",
"               || []  []|\\  \r\n",
"               ||##O#####]   \r\n",
"               || []  []|/   \r\n",
"                             \r\n",
"                             \r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",

"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"''''''''''''''''''''''''''''''''''''''''''''''''''''''''| | |\r\n",
"                                 \r\n",
"                                 \r\n",
"                                 \r\n",
"                                 \r\n",
"                                 \r\n",
"                                 \r\n",
"                    || []  []|\\ \r\n",
"m by Daniel Jennings||##O#####] \r\n",
"                    || []  []|/ \r\n",
"                                 \r\n",
"                   || []  []|\\  \r\n",
"                   ||##O#####]   \r\n",
"                   || []  []|/   \r\n",
"                                 \r\n",
"                                 \r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",

"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"''''''''''''''''''''''''''''''''''''''''''''''''''''| | | | |\r\n",
"                                     \r\n",
"                                     \r\n",
"                                     \r\n",
"                                     \r\n",
"                                     \r\n",
"                                     \r\n",
"                                     \r\n",
"rogram by Daniel Jennings|| []  []|\\ \r\n",
"                         ||##O#####] \r\n",
"                         || []  []|/ \r\n",
"                       || []  []|\\  \r\n",
"                       ||##O#####]   \r\n",
"                       || []  []|/   \r\n",
"                                     \r\n",
"                                     \r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",

"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"''''''''''''''''''''''''''''''''''''''''''''''''| | | | | | |\r\n",
"                                           \r\n",
"                                           \r\n",
"                                           \r\n",
"                                           \r\n",
"                                           \r\n",
"                                           \r\n",
"                                           \r\n",
"oddy program by Daniel Jennings|| []  []|\\ \r\n",
"                               ||##O#####] \r\n",
"                               || []  []|/ \r\n",
"                                   o. []   \r\n",
"                              || []   .|\\ \r\n",
"                              ||##O#####]  \r\n",
"                              || []  \\|/  \r\n",
"                                           \r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",

"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"''''''''''''''''''''''''''''''''''''''''''''| | | | | | | | |\r\n",
"                                                 \r\n",
"                                                 \r\n",
"                                                 \r\n",
"                                                 \r\n",
"                                                 \r\n",
"                                                 \r\n",
"                                                 \r\n",
"er shoddy program by Daniel Jennings|| []  []|\\ \r\n",
"                                    ||##O#####] \r\n",
"                                    || []  []|/ \r\n",
"                                         []      \r\n",
"                                    Oo..         \r\n",
"                                 || []   .|\\    \r\n",
"                                 ||##O#####]     \r\n",
"                                 || []  \\|/     \r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",

"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"''''''''''''''''''''''''''''''''''''''''| | | | | | | | | | |\r\n",
"                                                       \r\n",
"                                                       \r\n",
"                                                       \r\n",
"                                                       \r\n",
"                                                       \r\n",
"                                                       \r\n",
"                                                       \r\n",
" Another shoddy program by Daniel Jennings|| []  []|\\ \r\n",
"                                          ||##O#####]  \r\n",
"                                          || []  []|/  \r\n",
"                                             []        \r\n",
"                                                       \r\n",
"                                    OOOo..             \r\n",
"                                   || []   .|\\        \r\n",
"                                   ||##O#####]         \r\n",
"'''''''''''''''''''''''''''''''''''|| []  \\|/'''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",

"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"''''''''''''''''''''''''''''''''''''| | | | | | | | | | | | |\r\n",
"                                                             \r\n",
"                                                             \r\n",
"                                                             \r\n",
"                                                             \r\n",
"                                                             \r\n",
"                                                             \r\n",
"                                                             \r\n",
"       Another shoddy program by Daniel Jennings|| []  []|\\  \r\n",
"                                                ||##O#####]  \r\n",
"                                                || []  []|/  \r\n",
"                                                []           \r\n",
"                                                             \r\n",
"                                                             \r\n",
"                                       OOOOo.                \r\n",
"                                      || []   .|\\            \r\n",
"''''''''''''''''''''''''''''''''''''''||##O#####]''''''''''''\r\n",
"''''''''''''''''''''''''''''''''''''''|| []  \\|/''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",

"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"''''''''''''''''''''''''''''''''| | | | | | | | | | | | | | |\r\n",
"                                                             \r\n",
"                                                             \r\n",
"                                                             \r\n",
"                                                             \r\n",
"                                                             \r\n",
"                                                             \r\n",
"                                                             \r\n",
"       Another shoddy program by Daniel Jennings    || []  []\r\n",
"                                                    ||##O####\r\n",
"                                                    || []  []\r\n",
"                                                   []        \r\n",
"                                                             \r\n",
"                                                             \r\n",
"                                                             \r\n",
"                                         OOOOo.              \r\n",
"''''''''''''''''''''''''''''''''''''''''|| []   .|\\''''''''''\r\n",
"''''''''''''''''''''''''''''''''''''''''||##O#####]''''''''''\r\n",
"''''''''''''''''''''''''''''''''''''''''|| []  \\|/''''''''''\r\n",

"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"''''''''''''''''''''''''''''| | | | | | | | | | | | | | | | |\r\n",
"                                                             \r\n",
"                                                             \r\n",
"                                                             \r\n",
"                                                             \r\n",
"                                                             \r\n",
"                                                             \r\n",
"                                                             \r\n",
"       Another shoddy program by Daniel Jennings       || [] \r\n",
"                                                       ||##O#\r\n",
"                                                    [] || [] \r\n",
"                                                             \r\n",
"                                                             \r\n",
"                                                             \r\n",
"                                                             \r\n",
"                                                             \r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''|| []   .|\\'''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''||##O#####]'''''''''\r\n",

"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"''''''''''''''''''''''''| | | | | | | | | | | | | | | | | | |\r\n",
"                                                             \r\n",
"                                                             \r\n",
"                                                             \r\n",
"                                                             \r\n",
"                                                             \r\n",
"                                                             \r\n",
"                                                             \r\n",
"       Another shoddy program by Daniel Jennings          || \r\n",
"                                                      []  ||#\r\n",
"                                                          || \r\n",
"                                                             \r\n",
"                                                             \r\n",
"                                                             \r\n",
"                                                             \r\n",
"                                                             \r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"''''''''''''''''''''''''''''''''''''''''''|| []   .|\\''''''''\r\n",

"'''''''''''''''''''''''''''''''''''''''''''''''''''''''/_/   \r\n",
"''''''''''''''''''''''''''''''''''''''''''''''''''''''/_/    \r\n",
"''''''''''''''''''''''''| | | | | | | | | | | | | | | /      \r\n",
"                                                             \r\n",
"                                                             \r\n",
"                                                             \r\n",
"                                                             \r\n",
"                                                             \r\n",
"                                                             \r\n",
"                                                             \r\n",
"       Another shoddy program by Daniel Jennings        []   \r\n",
"                                                             \r\n",
"                                                             \r\n",
"                                                             \r\n",
"                                                             \r\n",
"                                                            '\r\n",
"                                                           ''\r\n",
"                                                         ''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",

"'''''''''''''''''''''''''''''''''''''''''''''''''''/_/       \r\n",
"''''''''''''''''''''''''''''''''''''''''''''''''''/_/        \r\n",
"''''''''''''''''''''| | | | | | | | | | | | | | | /          \r\n",
"                                                             \r\n",
"                                                             \r\n",
"                                                             \r\n",
"                                                             \r\n",
"                                                             \r\n",
"                                                             \r\n",
"                                                             \r\n",
"       Another shoddy program by Daniel Jennings          [] \r\n",
"                                                             \r\n",
"                                                             \r\n",
"                                                             \r\n",
"                                                           ''\r\n",
"                                                          '''\r\n",
"                                                        '''''\r\n",
"                                                     ''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",

"'''''''''''''''''''''''''''''''''''''''''''''''/_/           \r\n",
"''''''''''''''''''''''''''''''''''''''''''''''/_/            \r\n",
"''''''''''''''''| | | | | | | | | | | | | | | /              \r\n",
"                                                             \r\n",
"                                                             \r\n",
"                                                             \r\n",
"                                                             \r\n",
"                                                             \r\n",
"                                                             \r\n",
"                                                             \r\n",
"       Another shoddy program by Daniel Jennings           []\r\n",
"                                                             \r\n",
"                                                             \r\n",
"                                                           ''\r\n",
"                                                       ''''''\r\n",
"                                                      '''''''\r\n",
"                                                    '''''''''\r\n",
"                                                 ''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",

"'''''''''''''''''''''''''''''''''''''''''''/_/               \r\n",
"''''''''''''''''''''''''''''''''''''''''''/_/                \r\n",
"''''''''''''| | | | | | | | | | | | | | | /                  \r\n",
"                                                             \r\n",
"                                                             \r\n",
"                                                             \r\n",
"                                                             \r\n",
"                                                             \r\n",
"                                                             \r\n",
"                                                             \r\n",
"       Another shoddy program by Daniel Jennings          [] \r\n",
"                                                             \r\n",
"                                                         ''''\r\n",
"                                                     ''''''''\r\n",
"                                                   ''''''''''\r\n",
"                                                  '''''''''''\r\n",
"                                                '''''''''''''\r\n",
"                                             ''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",

"'''''''''''''''''''''''''''''''''''''''/_/                   \r\n",
"''''''''''''''''''''''''''''''''''''''/_/                    \r\n",
"''''''''| | | | | | | | | | | | | | | /                      \r\n",
"                                                             \r\n",
"                                                             \r\n",
"                                                             \r\n",
"                                                             \r\n",
"                                                             \r\n",
"                                                             \r\n",
"                                                             \r\n",
"       Another shoddy program by Daniel Jennings        [] ''\r\n",
"                                                         ''''\r\n",
"                                                     ''''''''\r\n",
"                                                 ''''''''''''\r\n",
"                                               ''''''''''''''\r\n",
"                                              '''''''''''''''\r\n",
"                                            '''''''''''''''''\r\n",
"                                         ''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",

"'''''''''''''''''''''''''''''''''''''''/_/                   \r\n",
"''''''''''''''''''''''''''''''''''''''/_/                    \r\n",
"''''''''| | | | | | | | | | | | | | | /                      \r\n",
"                                                             \r\n",
"                                                             \r\n",
"                                                             \r\n",
"                                                             \r\n",
"                                                             \r\n",
"                                                             \r\n",
"                                                             \r\n",
"       Another shoddy program by Daniel Jennings      []'''''\r\n",
"                                                    '''''''''\r\n",
"                                                '''''''''''''\r\n",
"                                            '''''''''''''''''\r\n",
"                                          '''''''''''''''''''\r\n",
"                                         ''''''''''''''''''''\r\n",
"                                       ''''''''''''''''''''''\r\n",
"                                    '''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",

"'''''''''''''''''''''''''''''''''''/_/                       \r\n",
"''''''''''''''''''''''''''''''''''/_/                        \r\n",
"''''| | | | | | | | | | | | | | | /                          \r\n",
"                                                             \r\n",
"                                                             \r\n",
"                                                             \r\n",
"                                                             \r\n",
"                                                             \r\n",
"                                                            '\r\n",
"                                                        '''''\r\n",
"       Another shoddy program by Daniel Jennings  '''[]''''''\r\n",
"                                            '''''''''''''''''\r\n",
"                                        '''''''''''''''''''''\r\n",
"                                    '''''''''''''''''''''''''\r\n",
"                                  '''''''''''''''''''''''''''\r\n",
"                                 ''''''''''''''''''''''''''''\r\n",
"                               ''''''''''''''''''''''''''''''\r\n",
"                            '''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",

"'''''''''''''''''''''''''''''''/_/                           \r\n",
"''''''''''''''''''''''''''''''/_/                            \r\n",
"| | | | | | | | | | | | | | | /                              \r\n",
"                                                             \r\n",
"                                                             \r\n",
"                                                             \r\n",
"                                                             \r\n",
"                                                            '\r\n",
"                                                        '''''\r\n",
"                                                    '''''''''\r\n",
"       Another shoddy program by Daniel Jennings''''[]'''''''\r\n",
"                                        '''''''''''''''''''''\r\n",
"                                    '''''''''''''''''''''''''\r\n",
"                                '''''''''''''''''''''''''''''\r\n",
"                              '''''''''''''''''''''''''''''''\r\n",
"                             ''''''''''''''''''''''''''''''''\r\n",
"                           ''''''''''''''''''''''''''''''''''\r\n",
"                        '''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",

"'''''''''''''''''''''''''''/_/                              '\r\n",
"''''''''''''''''''''''''''/_/                               '\r\n",
"| | | | | | | | | | | | | /                                 '\r\n",
"                                                            '\r\n",
"                                                            '\r\n",
"                                                            '\r\n",
"                                                            '\r\n",
"                                                        '''''\r\n",
"                                                    '''''''''\r\n",
"                                                '''''''''''''\r\n",
"       Another shoddy program by Daniel Jennings'[]''''''''''\r\n",
"                                    '''''''''''''''''''''''''\r\n",
"                                '''''''''''''''''''''''''''''\r\n",
"                            '''''''''''''''''''''''''''''''''\r\n",
"                          '''''''''''''''''''''''''''''''''''\r\n",
"                         ''''''''''''''''''''''''''''''''''''\r\n",
"                       ''''''''''''''''''''''''''''''''''''''\r\n",
"                    '''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",

"'''''''''''''''''''''''/_/                              '''''\r\n",
"''''''''''''''''''''''/_/                               '''''\r\n",
"| | | | | | | | | | | /                                 '''''\r\n",
"                                                        '''''\r\n",
"                                                        '''''\r\n",
"                                                        '''''\r\n",
"                                                        '''''\r\n",
"                                                    '''''''''\r\n",
"                                                '''''''''''''\r\n",
"                                            '''''''''''''''''\r\n",
"       Another shoddy program by Daniel'Jennin[]'''''''''''''\r\n",
"                                '''''''''''''''''''''''''''''\r\n",
"                            '''''''''''''''''''''''''''''''''\r\n",
"                        '''''''''''''''''''''''''''''''''''''\r\n",
"                      '''''''''''''''''''''''''''''''''''''''\r\n",
"                     ''''''''''''''''''''''''''''''''''''''''\r\n",
"                   ''''''''''''''''''''''''''''''''''''''''''\r\n",
"                '''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",

"'''''''''''''''''''/_/                              '''''''''\r\n",
"''''''''''''''''''/_/                               '''''''''\r\n",
"| | | | | | | | | /                                 '''''''''\r\n",
"                                                    '''''''''\r\n",
"                                                    '''''''''\r\n",
"                                                    '''''''''\r\n",
"                                                    '''''''''\r\n",
"                                                '''''''''''''\r\n",
"                                            '''''''''''''''''\r\n",
"                                        '''''''''''''''''''''\r\n",
"       Another shoddy program by Daniel'Jenn[]'''''''''''''''\r\n",
"                            '''''''''''''''''''''''''''''''''\r\n",
"                        '''''''''''''''''''''''''''''''''''''\r\n",
"                    '''''''''''''''''''''''''''''''''''''''''\r\n",
"                  '''''''''''''''''''''''''''''''''''''''''''\r\n",
"                 ''''''''''''''''''''''''''''''''''''''''''''\r\n",
"               ''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"            '''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",

"'''''''''''''''/_/                              '''''''''''''\r\n",
"''''''''''''''/_/                               '''''''''''''\r\n",
"| | | | | | | /                                 '''''''''''''\r\n",
"                                                '''''''''''''\r\n",
"                                                '''''''''''''\r\n",
"                                                '''''''''''''\r\n",
"                                                '''''''''''''\r\n",
"                                            '''''''''''''''''\r\n",
"                                        '''''''''''''''''''''\r\n",
"                                    '''''''''''''''''''''''''\r\n",
"       Another shoddy program by'Daniel'[]'''''''''''''''''''\r\n",
"                    '''''''''''''''''''''''''''''''''''''''''\r\n",
"                '''''''''''''''''''''''''''''''''''''''''''''\r\n",
"            '''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"          '''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"         ''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"       ''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"    '''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",

"'''''''''''/_/                              '''''''''''''''''\r\n",
"''''''''''/_/                               '''''''''''''''''\r\n",
"| | | | | /                                 '''''''''''''''''\r\n",
"                                            '''''''''''''''''\r\n",
"                                            '''''''''''''''''\r\n",
"                                            '''''''''''''''''\r\n",
"                                            '''''''''''''''''\r\n",
"                                        '''''''''''''''''''''\r\n",
"                                    '''''''''''''''''''''''''\r\n",
"                                '''''''''''''''''''''''''''''\r\n",
"       Another shoddy program'by'Dan[]'''''''''''''''''''''''\r\n",
"                '''''''''''''''''''''''''''''''''''''''''''''\r\n",
"            '''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"        '''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"      '''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"     ''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"   ''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",

"'''''''/_/                              '''''''''''''''''''''\r\n",
"''''''/_/                               '''''''''''''''''''''\r\n",
"| | | /                                 '''''''''''''''''''''\r\n",
"                                        '''''''''''''''''''''\r\n",
"                                        '''''''''''''''''''''\r\n",
"                                        '''''''''''''''''''''\r\n",
"                                        '''''''''''''''''''''\r\n",
"                                    '''''''''''''''''''''''''\r\n",
"                                '''''''''''''''''''''''''''''\r\n",
"                            '''''''''''''''''''''''''''''''''\r\n",
"       Another shoddy program'by[]'''''''''''''''''''''''''''\r\n",
"            '''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"        '''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"    '''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"  '''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
" ''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",

"'''/_/                              '''''''''''''''''''''''''\r\n",
"''/_/                               '''''''''''''''''''''''''\r\n",
"| /                                 '''''''''''''''''''''''''\r\n",
"                                    '''''''''''''''''''''''''\r\n",
"                                    '''''''''''''''''''''''''\r\n",
"                                    '''''''''''''''''''''''''\r\n",
"                                    '''''''''''''''''''''''''\r\n",
"                                '''''''''''''''''''''''''''''\r\n",
"                            '''''''''''''''''''''''''''''''''\r\n",
"                        '''''''''''''''''''''''''''''''''''''\r\n",
"       Another shoddy'progra[]'''''''''''''''''''''''''''''''\r\n",
"        '''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"    '''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",

"_/                              '''''''''''''''''''''''''''''\r\n",
"/                               '''''''''''''''''''''''''''''\r\n",
"                                '''''''''''''''''''''''''''''\r\n",
"                                '''''''''''''''''''''''''''''\r\n",
"                                '''''''''''''''''''''''''''''\r\n",
"                                '''''''''''''''''''''''''''''\r\n",
"                                '''''''''''''''''''''''''''''\r\n",
"                            '''''''''''''''''''''''''''''''''\r\n",
"                        '''''''''''''''''''''''''''''''''''''\r\n",
"                    '''''''''''''''''''''''''''''''''''''''''\r\n",
"       Another'shoddy'pr[]'''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",

"                            '''''''''''''''''''''''''''''''''\r\n",
"                            '''''''''''''''''''''''''''''''''\r\n",
"                            '''''''''''''''''''''''''''''''''\r\n",
"                            '''''''''''''''''''''''''''''''''\r\n",
"                            '''''''''''''''''''''''''''''''''\r\n",
"                            '''''''''''''''''''''''''''''''''\r\n",
"                            '''''''''''''''''''''''''''''''''\r\n",
"                        '''''''''''''''''''''''''''''''''''''\r\n",
"                    '''''''''''''''''''''''''''''''''''''''''\r\n",
"                '''''''''''''''''''''''''''''''''''''''''''''\r\n",
"       Another'shodd[]'''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",

"                        '''''''''''''''''''''''''''''''''''''\r\n",
"                        '''''''''''''''''''''''''''''''''''''\r\n",
"                        '''''''''''''''''''''''''''''''''''''\r\n",
"                        '''''''''''''''''''''''''''''''''''''\r\n",
"                        '''''''''''''''''''''''''''''''''''''\r\n",
"                        '''''''''''''''''''''''''''''''''''''\r\n",
"                        '''''''''''''''''''''''''''''''''''''\r\n",
"                    '''''''''''''''''''''''''''''''''''''''''\r\n",
"                '''''''''''''''''''''''''''''''''''''''''''''\r\n",
"            '''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"      'Another's[]'''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",

"                    '''''''''''''''''''''''''''''''''''''''''\r\n",
"                    '''''''''''''''''''''''''''''''''''''''''\r\n",
"                    '''''''''''''''''''''''''''''''''''''''''\r\n",
"                    '''''''''''''''''''''''''''''''''''''''''\r\n",
"                    '''''''''''''''''''''''''''''''''''''''''\r\n",
"                    '''''''''''''''''''''''''''''''''''''''''\r\n",
"                    '''''''''''''''''''''''''''''''''''''''''\r\n",
"                '''''''''''''''''''''''''''''''''''''''''''''\r\n",
"            '''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"        '''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"   ''''Anoth[]'''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",

"                '''''''''''''''''''''''''''''''''''''''''''''\r\n",
"                '''''''''''''''''''''''''''''''''''''''''''''\r\n",
"                '''''''''''''''''''''''''''''''''''''''''''''\r\n",
"                '''''''''''''''''''''''''''''''''''''''''''''\r\n",
"                '''''''''''''''''''''''''''''''''''''''''''''\r\n",
"                '''''''''''''''''''''''''''''''''''''''''''''\r\n",
"                '''''''''''''''''''''''''''''''''''''''''''''\r\n",
"            '''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"        '''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"    '''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"''''''A[]''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",

"            '''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"            '''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"            '''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"            '''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"            '''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"            '''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"            '''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"        '''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"    '''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"''''[]'''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",

"    '''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"    '''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"    '''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"    '''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"    '''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"    '''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"    '''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"]''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",

"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",

"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",

"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",

"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",

"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",
"'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''\r\n",


};

	SetConsoleCursorPosition(GetStdHandle(STD_OUTPUT_HANDLE), COORD{ 0,0 });
	int start_i = 0;
	const int loop_start_frame = 0;
	bool hasNotified = false;
	while (  reporter.status != reporter_status_fatal_error 
		  && reporter.status != reporter_status_none )
	{
		if (reporter.status != reporter_status_reporting)
		{
			hasNotified = false;
			for (int i = start_i; i < ARRAYSIZE(car) / anim_frame_size; ++i)
			{
				for (int j = 0; j < anim_frame_size; ++j)
				{
					printf(car[i * anim_frame_size + j]);
				}
				printf("\r\n%s\r\n", GetReporterStatusString(reporter.status));
				Sleep(145);
				SetConsoleCursorPosition(GetStdHandle(STD_OUTPUT_HANDLE), COORD{ 0,0 });
			}
		}
		else if(!hasNotified && reporter.status == reporter_status_reporting)
		{
			hasNotified = true;
			system("cls");
			printf("%s\r\n", GetReporterStatusString(reporter.status));
		}

		start_i = loop_start_frame;
	}


	if(reporter_thread.joinable())
		reporter_thread.join();

	if (midi_thread.joinable())
		midi_thread.join();

	system("pause");
}
