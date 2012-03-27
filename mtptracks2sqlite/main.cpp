#include "libmtp.h"
#include <stdlib.h>
#include <string.h>
#include <exception>
#include <sqlite3.h>

using namespace std;

#define CREATE_QUERY "CREATE TABLE `scrobblings` (`artist` VARCHAR(30) COLLATE NOCASE,`title` VARCHAR(50) COLLATE NOCASE,`count` INTEGER2, `duration` INTEGER8, PRIMARY KEY(`artist`,`title`))"


int existingPlaycount = -1;
/**
 * Callback function which executes by sqlite3_exec in trackinfoToSqlite 
 * while getting track playcount, which is already in database. Populates global
 * variable "existingPlaycount" with value from database.
 * @param NotUsed
 * @param argc
 * @param argv
 * @param azColName
 * @return 
 */
int getCountFromDB(void *NotUsed, int argc, char **argv, char **azColName)
{
    sscanf(argv[0],"%d",&existingPlaycount);
    return 0;
}
/**
 * Updates playcount in database for track supplied (if it already exists there),
 * adding playcount from track object to playcount, existing in database. If track
 * (identified by combination of artist and title) isn`t in database yet, it will
 * be added there. INSERT or UPDATE statments are part of TRANSACTION, which is opened
 * by that function, so it must be COMMITted or ROLLBACKed later (ROLLBACK will be
 * necessary if an error occures while zeroing playcount in device after adding
 * information into database).
 * @param track Link to LIBMTP track object to add to database
 * @param db opened database handle
 * @return 1 if track was successfully added, 0 - if track object doesn`t
 * contains information about artist or title (track->artist or track->title
 * are NULL) or playcount (track->usecount) is zero, so there is no reason to
 * add track to database.
 * @thwors exception if an error occures while executong SQL statement. Error
 * description is outputed to stderr. If an error occurs while getting
 * information from db, transaction won`t be started, otherwise - will (but
 * won`t be rollbacked before throwing execption).
 */
short trackinfoToSqlite(LIBMTP_track_t *track, sqlite3* db)
{
    if(track->artist == NULL || track->title == NULL || track->usecount == 0)
        return 0;

    char *query;
    char *dbErrMsg;

    // sqlite3_mprintf will take care about NULLs, escaping "'" and adding "'" around strings
    query = sqlite3_mprintf("SELECT `count` FROM `scrobblings` WHERE `artist` = %Q AND `title`= %Q",
            track->artist,
            track->title);

    // set existingPlaycount = -1 to know, whether getCountFromDB were never executed (track not
    // exists in DB yet)
    existingPlaycount = -1;

    // if track already exists in DB -> getCountFromDB will be called and will update global
    // variable existingPlaycount
    if(SQLITE_OK != sqlite3_exec(db,query,getCountFromDB,NULL,&dbErrMsg))
        {
        fprintf(stderr, "Can't get information about track from database: %s\n",
                dbErrMsg);
        sqlite3_free(dbErrMsg);
        sqlite3_free(query);
        throw new exception();
        }
    sqlite3_free(query);

    // now beginning a transaction, which will be rollbacked if an error occures where zeroing playcount in device (outside this function)
    if(SQLITE_OK != sqlite3_exec(db,"BEGIN TRANSACTION",NULL,NULL,&dbErrMsg))
        {
        fprintf(stderr, "Can't begin transaction before adding playcount info for track: %s\n",
                dbErrMsg);
        sqlite3_free(dbErrMsg);
        throw new exception();
        }

    if(existingPlaycount == -1) // track not in db
        query = sqlite3_mprintf("INSERT INTO `scrobblings`(`artist`,`title`,`count`,`duration`) VALUES (%Q,%Q,%u,%d);",
                track->artist,
                track->title,
                track->usecount,
                track->duration);
    else // track already in DB - update playcount, adding to it value, got from device
        query = sqlite3_mprintf("UPDATE `scrobblings` SET `count` = %u WHERE `artist` = %Q AND `title`= %Q",
                track->usecount + 100,//existingPlaycount,
                track->artist,
                track->title);

    // now - executing query (INSERT OR UPDATE)
    if(SQLITE_OK != sqlite3_exec(db,query,NULL,NULL,&dbErrMsg))
        {
        fprintf(stderr, "Can't insert new row into database or update existing: %s\n",
                dbErrMsg);
        sqlite3_free(dbErrMsg);
        sqlite3_free(query);
        throw new exception();
        }

    sqlite3_free(query);

    return 1;
}


short trackinfoToSqlDump(LIBMTP_track_t *track)
{
    if(track->artist == NULL || track->title == NULL || track->usecount == 0)
        return 0;

    char *query;
    char *dbErrMsg;

    query = sqlite3_mprintf("INSERT INTO `scrobblings`(`artist`,`title`,`count`,`duration`,`album`) VALUES (%Q,%Q,%u,%d,%Q);",
            track->artist,
            track->title,
            track->usecount,
            track->duration,
            track->album);

    printf("%s\n",query);

    sqlite3_free(query);

    return 1;
}

/**
 * Opens or creates db with filename specified. If db file doesn`t exists - it
 * will be created and table for scrobling will be created (statement, that
 * creates the table is in CREATE_QUERY constant)
 * @param dbFileName file name of existing db file or a file to save to new db
 * @return handle of database connection on success or NULL on failure. If
 * an error occures, error cause will be outputed to stderr.
 */
sqlite3* dbInit(char *dbFileName)
{
    sqlite3 *db;
    int dbOpenResult;
    char *dbErrMsg = 0;
    // check whether db file already exists
    if(-1 == access(dbFileName, 0)) // not exists, create new database file and table structure
        {
        dbOpenResult = sqlite3_open(dbFileName,&db);
        if(dbOpenResult != SQLITE_OK)
            {
            fprintf(stderr, "Can't create non-existing database %s: %s\n",
                    dbFileName,
                    sqlite3_errmsg(db));
            sqlite3_close(db);
            return NULL;
            }
        // create table (execute CREATE_QUERY)
        if(SQLITE_OK != sqlite3_exec(db,CREATE_QUERY,NULL,NULL,&dbErrMsg))
            {
            fprintf(stderr, "Can't create table in non-existing database %s: %s\n",
                    dbFileName,
                    dbErrMsg);
            sqlite3_free(dbErrMsg);
            sqlite3_close(db);
            return NULL;
            }
        }
    else // exists - open database
        {
        dbOpenResult = sqlite3_open(dbFileName,&db);
        if(dbOpenResult != SQLITE_OK)
            {
            fprintf(stderr, "Can't open existing database %s: %s\n",
                    dbFileName,
                    sqlite3_errmsg(db));
            sqlite3_close(db);
            return NULL;
            }
        }
    return db;
}


/**
 * This program reads information from MTP-device and updates sqlite database,
 * which file is specified by first parameter. If database file doesn`t exist,
 * database will be created and table `scrobblings` with columns `artist`,
 * `title`, `count` and `duration` will be added to it.  Otherwise, existing
 * db will be used (assuming that table `scrobblings` exists to).
 * Only tracks with non NULL artist and title and non-zero playcount are added
 * to database. If track with this combination of artist and title already
 * exists in database, its playcount will be increased by the value, got
 * from device. After successfully updating database, playcount in device will
 * be set to zero (as it`s already token in account and should not be used
 * twice). If an error occures while setting playcount in device to zero for
 * some track, changes, made for it in db, will be rollbacked to leave the
 * system in integtiry. All error messages are outputed to stderr, information
 * messages - to stdout.
 * SQLite table create query is following:
 * CREATE TABLE `scrobblings`
 *      (
 *      `artist` VARCHAR(30) COLLATE NOCASE,
 *      `title`  VARCHAR(50) COLLATE NOCASE,
 *      `count` INTEGER2,
 *      `duration` INTEGER8,
 *      PRIMARY KEY(`artist`,`title`)
 *      )
 * @param argc must be equal to 2
 * @param argv must contain (except argv[0]) the only comandline parameter -
 * name of db file
 * @return 0 on success, 1 on error
 */
int main (int argc, char **argv)
{
	/*if(argc != 2)
		{
		fprintf(stderr, "Usage: %s output_db_file_name, where \"output_db_file_name\" - name of an output sqlite database file (if doesn`t exists, it will be created)\n", argv[0]);
		return 1;
		}

        sqlite3 *db = dbInit(argv[1]);
        if(db == NULL) // an error occured wile connecting to db - exiting
            exit(1);*/
        

	LIBMTP_mtpdevice_t *device_list, *iter;
	LIBMTP_track_t *tracks;

	LIBMTP_Init();
	//fprintf(stdout, "Attempting to connect device(s)\n");

	switch(LIBMTP_Get_Connected_Devices(&device_list))
            {
            /* no devices found - close db end exit with "success" (0) */
            case LIBMTP_ERROR_NO_DEVICE_ATTACHED:
                fprintf(stdout, "No Devices have been found, exiting\n");
                //sqlite3_close(db);
                exit(0);
            /* Error cases - print cause, close db and exit with "1"*/
            case LIBMTP_ERROR_CONNECTING:
                fprintf(stderr, "There has been an error connecting, exiting\n");
                //sqlite3_close(db);
                exit(1);
            case LIBMTP_ERROR_MEMORY_ALLOCATION:
                fprintf(stderr, "Memory Allocation Error, exiting\n");
                //sqlite3_close(db);
                exit(1);
            /* Unknown general errors - This should never execute */
            case LIBMTP_ERROR_GENERAL:
                default:
                fprintf(stderr, "Unknown error, please report "
                            "this to the libmtp developers, exiting\n");
                //sqlite3_close(db);
                exit(1);

            /* Successfully connected at least one device, so continue */
            case LIBMTP_ERROR_NONE:
                break;
                //fprintf(stdout, "Successfully connected, now will iterate through devices\n");
                //fflush(stdout);
            }

	/* iterate through connected MTP devices */
	for(iter = device_list; iter != NULL; iter = iter->next)
            {
            char *friendlyname;
            /* Echo the friendly name so we know which device we are working with */
            friendlyname = LIBMTP_Get_Friendlyname(iter);
            /*if (friendlyname == NULL)
                {
                printf("Friendly device name: (NULL)\n");
                }
            else
                {
                printf("Friendly device name: %s\n", friendlyname);
                free(friendlyname);
                }*/

	//printf("Now will iterate through tracks (this may take some minutes)\n");
        // Get track listing.
        tracks = LIBMTP_Get_Tracklisting_With_Callback(iter, NULL, NULL);
        if (tracks == NULL)
            {
            printf("No tracks on this device. Going next one.\n");
            }
        else
            {
            LIBMTP_track_t *track, *tmp;
            char *dbErrMsg;

            track = tracks;
            try
		        {
		        while (track != NULL)
		            {
		            try
		                {
		                // save info about track in DB.
                                //trackinfoToSqlDump
		                //if(1 == trackinfoToSqlite(track,db)) // track was added to DB - set playcount in device to 0
		                if(1 == trackinfoToSqlDump(track))
                                    {
		                    track->usecount = 0;
		                    if(0 != LIBMTP_Update_Track_Metadata(iter,track))
		                        {
		                        fprintf(stderr, "Can`t update playcount in device for song %s - %s. Rollbacking changes in db for this track.\n",
		                                track->artist, // artinst & title are not NULL cause otherwise we would not get here (trackinfoToSqlite would return 0)
		                                track->title);
		                        /* Cause an error occured while updating playcount in device,
		                         * we need to rollback playcount in database to it`s state
		                         * before running the program (because as playcount in device
		                         * isn`t updated, further running will increase value in DB again, so
		                         * it will be increased twice). 
		                         */
		                        /*if(SQLITE_OK != sqlite3_exec(db,"ROLLBACK TRANSACTION",NULL,NULL,&dbErrMsg))
		                            {
		                            fprintf(stderr, "Can't rollback transaction after failure of updating playcount on device: %s, will retry..\n",
		                                    dbErrMsg); // retry will be in "catch" block
		                            sqlite3_free(dbErrMsg);
		                            throw new exception();
		                            }*/
		                        }
		                    /*else // commiting changes in database cause device updated successfully
		                        {
		                        while(SQLITE_OK != sqlite3_exec(db,"COMMIT TRANSACTION",NULL,NULL,&dbErrMsg))
		                            {
		                            fprintf(stderr, "Can't commit transaction after failure of updating playcount on device: %s. Retry after 2 seconds.\n",
		                                    dbErrMsg);
		                            sqlite3_free(dbErrMsg);
		                            sleep(2);
		                            }
		                        }*/
		                    }
		                
		                // destroy track
		                tmp = track;
		                track = track->next;
		                LIBMTP_destroy_track_t(tmp);

		                }
		            catch (exception& e) // We can get exception in call to trackinfoToSqlite or rollbacking transaction after device playcount update failure
		                {
		                /*
		                * Trying to rollback transaction (even it can be not started
		                * yet if an SQL error occures before BEGIN TRANSACTION statement)
		                */
		                /*if(SQLITE_OK != sqlite3_exec(db,"ROLLBACK TRANSACTION",NULL,NULL,&dbErrMsg))
		                    {
		                    fprintf(stderr, "Can't rollback transaction after getting an exception: %s, resetting db connection\n",
		                            dbErrMsg);
		                    sqlite3_free(dbErrMsg);
		                    // resetting db connection. Closing connection must rollback uncommited changes
		                    sqlite3_close(db);
		                    
		                    db = dbInit(argv[1]);
							if(db == NULL) // an error occured wile connecting to db - exiting
								throw new exception(); // will fall out from track loop
		                    }*/
		                
		                // destroy track
		                tmp = track;
		                track = track->next;
		                LIBMTP_destroy_track_t(tmp);
		                }

		            }
		        }
		    catch(exception& e) // exception may be thrown from loop`s "catch" block, if ROLLBACK fails
				{
				fprintf(stderr, "Cann`t reconnect to db. Exiting\n");
				}
            }
	}

    // release dev list and close db
	LIBMTP_Release_Device_List(device_list);
	/*if(NULL != db) // can be NULL if reconnect attemp failed
		{
		sqlite3_close(db);       
		printf("Task acomplished successfully. Now all your tracks are in %s sqlite file.\n", argv[1]);
		}*/
	exit(0);
}
