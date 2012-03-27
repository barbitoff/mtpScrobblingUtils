<?php
	$db_host = "localhost";
	$db_user = "root";
	$db_psw = "xxxxxx";
	$db_name = "mtp2lastfm";
	
	$startTime = mktime(6,00,0,8,09,2011);
	$startTime-= 4*60*60; // fixing timezone

	$dblink = mysql_connect($db_host, $db_user, $db_psw) or die("Подключение не удалось: " . mysql_error());
	if(!mysql_select_db($db_name, $dblink))
		{
		mysql_close($dblink);
		die("Невозможно выбрать базу $db_name!<br>\n");
		}
	try
		{
		mysql_query("set names utf8");
		$outFile = fopen("scrobble.sh","w");
		if(!$outFile)
			throw new Exception("Error opening file for writing");
		if(false === fwrite($outFile, "#!/bin/bash\n"))
			throw new Exception("Error writing #!/bin/bash header. Aborting<br/>\n");
	
		$exit = false;
		$mainSelectQuery = "SELECT `artist`, `title`, `count`, `duration`,`scrobid`, `album` FROM `scrobblings` WHERE `count` > 0 ORDER BY RAND()";
		$res = mysql_query($mainSelectQuery);
		if(!$res)
			throw new Exception("Error executing query: ".mysql_error());
		while(!$exit && (mysql_num_rows($res)!=0))
			{
			while(($row = mysql_fetch_array($res)) && !$exit)
				{
				$length = sprintf("%d:%02d",floor($row[3]/60000),floor($row[3]/1000) % 60);		
				echo "/usr/lib/lastfmsubmitd/lastfmsubmit -a \"$row[0]\" --title \"$row[1]\" -l \"$length\" --album \"$row[5]\" --time \"".date("Y-m-d H:i:s",$startTime)."\"<br/>\n";		
				if(false === fwrite($outFile, "/usr/lib/lastfmsubmitd/lastfmsubmit -a \"$row[0]\" --title \"$row[1]\" -l \"$length\" --time \"".date("Y-m-d H:i:s",$startTime)."\"\n"))
					{
					mysql_free_result($res);
					throw new Exception("Write error!\n");
					}
				// track written, increasing start time of next track
				$startTime+= floor($row[3]/1000);
				// decriment playcount of track
				$query = "UPDATE `scrobblings` SET `count`=`count`-1 WHERE `scrobid`=$row[4]";
				if(!mysql_query($query))
					{
					mysql_free_result($res);
					throw new Exception("Error decrimenting playcount of track: ".mysql_error());
					}
				if($startTime >= time()-4*60*60) // time is greater than now (fixing timezone), cann`t scrobble any more
					{
					echo "Cann`t scrobble any more, time reached now<br/>\n";
					$exit = true;					
					}
				}
			mysql_free_result($res);
							
			// another pass - get tracks which still have non zero playcount
			if(!$exit) // if exit is set - no need to query any more
				{
				$res = mysql_query($mainSelectQuery);
				if(!$res)
					throw new Exception("Error executing query: ".mysql_error());	
				}		
			}
		fclose($outFile);	
		}
	catch(Exception $e)
		{
		print($e->getMessage());
		if($outFile)
			fclose($outFile);
		}

	mysql_close($dblink);
?>
