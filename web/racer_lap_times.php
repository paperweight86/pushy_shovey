<!DOCTYPE html>
<html>
<head>
<style> 
body
{
	background-color: #000000;
	color: #ffffff;
    text-align: center;
}

select {
    //-webkit-appearance: button;
    //-moz-appearance: button;
    //-webkit-user-select: none;
    //-moz-user-select: none;
    -webkit-padding-end: 20px;
    -moz-padding-end: 20px;
    -webkit-padding-start: 2px;
    -moz-padding-start: 2px;
    background-color: transparent; /* fallback color if gradients are not supported */
    background-position: center right;
    background-repeat: no-repeat;
    border: 0px solid #AAA;
    //border-radius: 2px;
    box-shadow: 0px 1px 3px rgba(0, 0, 0, 0.1);
    color: #FFF;
    font-size: inherit;
    margin: 0;
    overflow: hidden;
    padding-top: 2px;
    padding-bottom: 2px;
    text-overflow: ellipsis;
    white-space: nowrap;
    font-family: Arial,"Helvetica Neue",Helvetica,sans-serif;
    font-size: 32px;
}

select option {
    background-color: #000;
    color: #FFF;
    font-family: Arial,"Helvetica Neue",Helvetica,sans-serif;
    font-size: 32px;
}

button
{
    background-color: #000;
    color: #FFF;
    font-family: Arial,"Helvetica Neue",Helvetica,sans-serif;
    font-size: 32px; 
    border: 1px solid #FFF;
}

h1
{
    text-align: center;
    font-family: Arial,"Helvetica Neue",Helvetica,sans-serif;
}

.page-aligner
{
    display: -webkit-flex;
    display: flex;
    align-items: center;
    justify-content: center;
}

.lap-container {
    display: -webkit-flex;
    display: flex;
    flex-direction: column;
    font-family: Arial,"Helvetica Neue",Helvetica,sans-serif;
    font-size: 14px;
    align-items: flex-start;
    text-align: left;
    //border: 1.5px solid #F0F;
}

.lap-item {
    background-color: #333;
}

.lap-time-container-header {
    background-color: lightgrey;
    display: -webkit-flex;
    display: flex;
    width: 508px;
    font-family: Arial,"Helvetica Neue",Helvetica,sans-serif;
    font-size: 24px;
    color: #000000;
    margin: 4px;
    padding: 4px;
    font-weight: bold;
    //border: 1.5px solid #888;
}

.lap-time-container-0 {
    background-color: #2dbfed;
    display: -webkit-flex;
    display: flex;
    width: 508px;
    font-family: Arial,"Helvetica Neue",Helvetica,sans-serif;
    font-size: 24px;
    margin: 4px;
    padding: 4px;
}

.lap-time-container-1 {
    background-color: #2dbfed;
    display: -webkit-flex;
    display: flex;
    width: 508px;
    font-family: Arial,"Helvetica Neue",Helvetica,sans-serif;
    font-size: 24px;
    color: #ffffff;
    margin: 4px;
    padding: 4px;
}

.lap-time-container-1-place {
    background-color: #ecbf2f;
    display: -webkit-flex;
    display: flex;
    width: 508px;
    font-family: Arial,"Helvetica Neue",Helvetica,sans-serif;
    font-size: 24px;
    color: #000000;
    margin: 4px;
    padding: 4px;
    vertical-align: center;
    justify-content: flex-start;
}

.lap-time-container-2-place {
    background-color: #2eef8f;
    display: -webkit-flex;
    display: flex;
    width: 508px;
    font-family: Arial,"Helvetica Neue",Helvetica,sans-serif;
    font-size: 24px;
    color: #000000;
    margin: 4px;
    padding: 4px;
}

.lap-time-container-3-place {
    background-color: #ec305e;
    display: -webkit-flex;
    display: flex;
    width: 508px;
    font-family: Arial,"Helvetica Neue",Helvetica,sans-serif;
    font-size: 24px;
    margin: 4px;
    padding: 4px;
}

.lap-date-item {
    flex-basis: 263px;
}

.lap-time-item {
    flex-basis: 200px;
    margin-left: 4px;
}

span {
    display: inline-block;
    vertical-align: middle;
}

</style>
</head>
<body>

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
//echo "Connected successfully<br>";

function timespan_to_string($seconds) {
  $ms = (int)($seconds * 1000.0);
  return sprintf('%02d:%02d:%02d', $ms/1000/60, ($ms/1000%60), ((int)(fmod($seconds,1) * 1000.0)));
}

$track_name = "Snetterton: 100 Circuit";
$car_name   = "W Motors Lykan HyperSport";
$racer_id = "2182080";

$track_id = "";
$car_id = "";
$racer_id = "";

if($_GET['track_id'] != "")
{
    $track_id = $conn->real_escape_string($_GET['track_id']);
}
else if($_GET['track'] != "")
{
    $track_name = $conn->real_escape_string($_GET['track']);
}

if($_GET['car_id'] != "")
{
    $car_id = $conn->real_escape_string($_GET['car_id']);
}
else if($_GET['car'] != "")
{
    $car_name = $conn->real_escape_string($_GET['car']);
}

if($_GET['racer_id'] != "")
{
    $racer_id = $conn->real_escape_string($_GET['racer_id']);
}

if($track_id == "")
{
    $track_sql = "SELECT id FROM `pcars_tracks` WHERE `track_name` LIKE '". $track_name ."'";

    $result = $conn->query($track_sql);
    if($result->num_rows > 0)
    {
    	$row = $result->fetch_assoc();
    	$track_id = $row['id'];
    }
}

if($car_id == "")
{
    $car_sql = "SELECT id FROM `pcars_cars` WHERE `car_name` LIKE '". $car_name ."'";

    $result = $conn->query($car_sql);
    if($result->num_rows > 0)
    {
    	$row = $result->fetch_assoc();
    	$car_id = $row['id'];
    }
}

echo "<form action=\"\">";

$all_tracks_sql = "SELECT id, track_name FROM `pcars_tracks`";

$result = $conn->query($all_tracks_sql);
if($result->num_rows > 0)
{
    echo "<select name=\"track_id\">";
    while($row = $result->fetch_assoc())
    {
        if($track_id != $row["id"])
            echo "    <option value=\"".$row["id"]."\">".$row["track_name"]."</option>";
        else
            echo "    <option value=\"".$row["id"]."\" selected=\"true\">".$row["track_name"]."</option>";
    }
    echo "</select>";
}

echo "</br>";

$all_cars_sql = "SELECT id, car_name FROM `pcars_cars`";

$result = $conn->query($all_cars_sql);
if($result->num_rows > 0)
{
    echo "<select name=\"car_id\">";
    while($row = $result->fetch_assoc())
    {
        if($car_id != $row["id"])
            echo "    <option value=\"".$row["id"]."\">".$row["car_name"]."</option>";
        else
            echo "    <option value=\"".$row["id"]."\" selected=\"true\">".$row["car_name"]."</option>";
    }
    echo "</select>";
}
echo "</br>";

$sql_racers = "SELECT * FROM `pcars_racers`";
$result = $conn->query($sql_racers);
$racer_dict = array();
if($result->num_rows > 0) 
{   
    echo "<select name=\"racer_id\">";
    while($row = $result->fetch_assoc())
    {
        $racer_dict[$row['steam_id']] = $row['steam_nickname'];
        if($racer_id != $row["steam_id"])
            echo "    <option value=\"".$row["steam_id"]."\">".$row["steam_nickname"]."</option>";
        else
            echo "    <option value=\"".$row["steam_id"]."\" selected=\"true\">".$row["steam_nickname"]."</option>";
    }
    echo "</select>";
}

echo "</br>";
echo "<button type=\"submit\">Show</button>";
echo "<form/>";
echo "</br>";
echo "</br>";

//echo "<h1>".$track_name."</br>";
//echo $car_name."</h1>";

echo "<div class=\"page-aligner\">";
echo "<div class=\"lap-container\">";
echo "<div class=\"lap-item\">";
echo "<div class=\"lap-time-container-header\">";
echo "<div class=\"lap-date-item\">";
echo "<span>Date</span>";
echo "</div>";
echo "<div class=\"lap-time-item\">";
echo "<span>Time</span>";
echo "</div>";

$sql = "SELECT * FROM `pcars_lap_times` WHERE racer_id = '".$racer_id."' AND car_id = '".$car_id."'  ORDER BY `date_time` DESC";
// TODO: [DanJ] Get fastest time and highlight in gold!

// LEFT JOIN `pcars_racers` on arse.`racer_id` = `pcars_racers`.`steam_id` LEFT JOIN `pcars_tracks` on arse.`track_id` = `pcars_tracks`.`id` LEFT JOIN `pcars_cars` on arse.`car_id` = `pcars_cars`.`id` WHERE `track_id`=". $track_id ." AND `car_id`=".$car_id." ORDER BY arse.`lap_time` ASC
$result = $conn->query($sql);
if($result->num_rows > 0) 
{   
    echo "</div>";
	echo "</div>";
    $i = 1;
	while($row = $result->fetch_assoc())
	{
		echo "<div class=\"lap-item\">";
		//if($i > 3)
        {
        	echo "<div class=\"lap-time-container-".($i%2)."\">";
        }
        //else 
        //{
        //	echo "<div class=\"lap-time-container-".$i."-place\">";
        //}
        echo "<div class=\"lap-date-item\"><span>";
		echo $row["date_time"];
		echo "</span></div>";
		echo "<div class=\"lap-time-item\"><span>";
		echo timespan_to_string($row["lap_time"]);
		echo "</span></div>";
		echo "</div>";
        echo "</div>";
        $i += 1;
	}
    echo "</div>";
    echo "</div>";
}
else
{
	echo "No results for track \"".$track_name."\" (".$track_id.") and car \"".$car_name."\" (".$car_id.") were found!";
}

$conn->close();
?>

</div>
</div>

</body>
</html>



</body>