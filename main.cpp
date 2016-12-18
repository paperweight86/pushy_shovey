
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

void report_thread(reporter_info* info)
{
	info->status = reporter_status_loading;
	// TODO: all this in another thread!

	bool write_file = false;
	const char* username = "pushy_reporter";
	const char* password = "pidgeonbeastquark";
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
	
	/*
	TODO:
	- save file per game with: <date>_<mode>_<track>_<car>.chod
	- masking off parts of the data - is that even possible?
	*/
	
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

		// We have to guess that the user is the one being viewed when we enter the game...
		bool have_steam_nickname = last_frame->m_viewed_racer_index != -1;
		char* steam_nickname = 0;
		char steam_id_buffer[max_string] = {};
		if (have_steam_nickname)
		{
			steam_nickname = last_frame->racers[last_frame->m_viewed_racer_index].m_name;
		}
		else
		{
			_itoa_s<max_string>(steam_id, steam_id_buffer, 10);
			steam_nickname = steam_id_buffer;
		}

		char query[max_string] = {};
		const char* insert_test_query_format = "INSERT INTO `test`.`pcars_racers` (`id`,`steam_nickname`,`steam_id`) VALUES (NULL, '%s', '%d') ON DUPLICATE KEY UPDATE `steam_nickname`=VALUES(`steam_nickname`);";
		
		char nickname_escaped[max_string] = {};
		unsigned long escape_result = mysql_real_escape_string(connect_result, nickname_escaped, steam_nickname, min(strlen(steam_nickname), max_string));
		sprintf_s<256>(query, insert_test_query_format, nickname_escaped, steam_id);
		int result = mysql_real_query(connect_result, query, strlen(query));

		if (result != 0)
		{
			info->status = reporter_status_fatal_error;
			printf("Error: unable to insert/update user");
		}

		bool current_lap_time_valid = false;
		bool last_lap_time_valid = false;

		while (	   info->status != reporter_status_fatal_error 
				&& info->status != reporter_status_none )
		{
			QueryPerformanceCounter(&EndingTime);
			ElapsedMicroseconds.QuadPart = EndingTime.QuadPart - StartingTime.QuadPart;

			ElapsedMicroseconds.QuadPart *= 1000000;
			ElapsedMicroseconds.QuadPart /= Frequency.QuadPart;

			// TODO: [DanJ] For what we want we can probably sample way lower than we are - quater second?
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

				// todo: if new lap
				if (this_frame->m_num_racers > 0
					&& this_frame->m_viewed_racer_index != -1
					&& this_frame->m_last_lap_time != -1.000)
				{
					if (this_frame->racers[this_frame->m_viewed_racer_index].m_current_lap > last_frame->racers[this_frame->m_viewed_racer_index].m_current_lap)
					{
						printf("Entering new lap #%d\n", this_frame->racers[this_frame->m_viewed_racer_index].m_current_lap);
						last_lap_time_valid = current_lap_time_valid;
						if (last_lap_time_valid)
						{
							printf("Last lap was valid - saving to db.\n");
							//this_frame->m_last_lap_time
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
								printf("Error: Failed to save lap in db.\n");
							}
							else
							{
								printf("Time %f seconds for %s: %s saved in db.\n", lap_time, this_frame->m_track_location, this_frame->m_track_variation);
							}
						}
						else
						{
							printf("Invalid previous lap.\n");
						}
					}
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

void main()
{
	//HMIDIOUT device;
	//int midiport = 0;
	//int flag = midiOutOpen(&device, midiport, 0, 0, CALLBACK_NULL);
	//if (flag != MMSYSERR_NOERROR) {
	//	printf("Error: could not open MIDI Output.\n");
	//}

	//union { unsigned long word; unsigned char data[4]; } message;
	//// message.data[0] = command byte of the MIDI message, for example: 0x90
	//// message.data[1] = first data byte of the MIDI message, for example: 60
	//// message.data[2] = second data byte of the MIDI message, for example 100
	//// message.data[3] = not used for any MIDI messages, so set to 0

	//message.data[0] = 0xC0;  // MIDI note-on message (requires to data bytes)
	//message.data[1] = 33;    // MIDI instrument change message: instrument 0-127
	//message.data[2] = 0;   // MIDI note-on message: Key velocity (100 = loud)
	//message.data[3] = 0;     // Unused parameter

	//flag = midiOutShortMsg(device, message.word);
	//if (flag != MMSYSERR_NOERROR) {
	//	printf("Error: could Output MIDI.\n");
	//}

	//message.data[0] = 0x90;  // MIDI note-on message (requires to data bytes)
	//message.data[1] = 35;    // MIDI note-on message: Key number (60 = middle C)
	//message.data[2] = 127;   // MIDI note-on message: Key velocity (100 = loud)
	//message.data[3] = 0;     // Unused parameter

	//flag = midiOutShortMsg(device, message.word);
	//if (flag != MMSYSERR_NOERROR) {
	//	printf("Error: could Output MIDI.\n");
	//}

	//Sleep(250);
	//message.data[0] = 0x90;  // MIDI note-on message (requires to data bytes)
	//message.data[1] = 25;    // MIDI note-on message: Key number (60 = middle C)
	//message.data[2] = 127;   // MIDI note-on message: Key velocity (100 = loud)
	//message.data[3] = 0;     // Unused parameter

	//flag = midiOutShortMsg(device, message.word);
	//if (flag != MMSYSERR_NOERROR) {
	//	printf("Error: could Output MIDI.\n");
	//}

	//Sleep(250);
	//message.data[0] = 0x90;  // MIDI note-on message (requires to data bytes)
	//message.data[1] = 45;    // MIDI note-on message: Key number (60 = middle C)
	//message.data[2] = 127;   // MIDI note-on message: Key velocity (100 = loud)
	//message.data[3] = 0;     // Unused parameter

	//flag = midiOutShortMsg(device, message.word);
	//if (flag != MMSYSERR_NOERROR) {
	//	printf("Error: could Output MIDI.\n");
	//}
	//Sleep(500);
	//message.data[0] = 0x90;  // MIDI note-on message (requires to data bytes)
	//message.data[1] = 35;    // MIDI note-on message: Key number (60 = middle C)
	//message.data[2] = 127;   // MIDI note-on message: Key velocity (100 = loud)
	//message.data[3] = 0;     // Unused parameter

	//flag = midiOutShortMsg(device, message.word);
	//if (flag != MMSYSERR_NOERROR) {
	//	printf("Error: could Output MIDI.\n");
	//}

	//Sleep(250);
	//message.data[0] = 0x90;  // MIDI note-on message (requires to data bytes)
	//message.data[1] = 25;    // MIDI note-on message: Key number (60 = middle C)
	//message.data[2] = 127;   // MIDI note-on message: Key velocity (100 = loud)
	//message.data[3] = 0;     // Unused parameter

	//flag = midiOutShortMsg(device, message.word);
	//if (flag != MMSYSERR_NOERROR) {
	//	printf("Error: could Output MIDI.\n");
	//}

	//Sleep(250);
	//message.data[0] = 0x90;  // MIDI note-on message (requires to data bytes)
	//message.data[1] = 40;    // MIDI note-on message: Key number (60 = middle C)
	//message.data[2] = 127;   // MIDI note-on message: Key velocity (100 = loud)
	//message.data[3] = 0;     // Unused parameter

	//flag = midiOutShortMsg(device, message.word);
	//if (flag != MMSYSERR_NOERROR) {
	//	printf("Error: could Output MIDI.\n");
	//}

	reporter_info reporter;
	std::thread reporter_thread (report_thread, &reporter);

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
	while (  reporter.status != reporter_status_fatal_error 
		  && reporter.status != reporter_status_none
		  && reporter.status != reporter_status_reporting)
	{
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

		start_i = loop_start_frame;
	}

	if (reporter.status == reporter_status_reporting)
	{
		system("cls");
		printf("%s\r\n", GetReporterStatusString(reporter.status));
	}

	if(reporter_thread.joinable())
		reporter_thread.join();

	system("pause");
}
