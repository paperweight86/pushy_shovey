<?php
$servername = "localhost";
$username = "pushy_viewer";
$password = "";
$database = "test";

// Create connection
$conn = new mysqli($servername, $username, $password, $database);

// Check connection
if ($conn->connect_error) {
    die("Connection failed: " . $conn->connect_error);
} 
echo "Connected successfully<br>";

function timespan_to_string($seconds) {
  $ms = (int)($seconds * 1000.0);
  return sprintf('%02d:%02d:%02d', $ms/1000/60, ($ms/1000%60), ((int)(fmod($seconds,1) * 1000.0)));
}

$sql = "SELECT * FROM `pcars_lap_times` LEFT JOIN `pcars_racers` on `pcars_lap_times`.`racer_id` = `pcars_racers`.`steam_id` LEFT JOIN `pcars_tracks` on `pcars_lap_times`.`track_id` = `pcars_tracks`.`id` LEFT JOIN `pcars_cars` on `pcars_lap_times`.`car_id` = `pcars_cars`.`id`";//"SELECT track_id, racer_id, car_id, lap_time, date_time from pcars_lap_times";
$result = $conn->query($sql);
if($result->num_rows > 0) 
{
	while($row = $result->fetch_assoc())
	{
		echo $row["steam_nickname"] ." ". "track: " . $row["track_name"]. " - car: " . $row["car_name"]. " time: " . timespan_to_string($row["lap_time"]) . " date: ". $row["date_time"] ."<br>";
	}
}

$conn->close();
?>