#include <gdata/gdata.h>
#include <glib-object.h>
#include <glib.h>

#include <sqlite3.h>

#include <CMulticalendar.h>
#include <CCalendar.h>
#include <CEvent.h>

#include <iostream>
#include <string>

#define CLIENT_ID "maemo-gcalsync-0.1"

struct shared_info
{
  sqlite3* db;
  gchar* googleac;
  GDataCalendarService *service;
  CCalendar *mcal;
  GHashTable *cal_mappings;
  GHashTable *event_mappings;
  GHashTable *accounts;
  GHashTable *forsync;
};

static GDataCalendarService* login_to_gcal(const gchar *username, const gchar *password, GError **error);
static GDataFeed* get_list_of_calendars(GDataCalendarService *service, GError **error);
static GDataFeed* get_list_of_events(GDataCalendarService *service, GDataCalendarCalendar *calendar, GError **error);
static void output_calendar(gpointer data, gpointer user_data);
static void add_event(gpointer data, gpointer user_data);
static void add_calendar(gpointer data, gpointer user_data);
sqlite3* connect_to_database(const char *filename);
void add_event_mapping(sqlite3* db, const gchar* gceventid, const string& meventid);
void add_calendar_mapping(struct shared_info* info, const gchar* gcalid, int mcalid);
void initialise_database(sqlite3* db);
void close_database(sqlite3* db);
void get_data(struct shared_info* info);
void add_account(struct shared_info *info, const gchar* username, const gchar* password);
void add_calendar_forsync(struct shared_info *info, const gchar* calid, int forsync);
void sync_calendars(struct shared_info *info, int argc, char** argv);
void sync_calendars_for_account(gpointer key, gpointer value, gpointer user_data);
static void check_event_difference(GDataCalendarEvent *gcevent, CEvent *mcevent);


static GDataCalendarService*
login_to_gcal(const gchar *username, const gchar *password, GError **error)
{
  GDataCalendarService *service;
  service = gdata_calendar_service_new (CLIENT_ID);
  if (!gdata_service_authenticate (GDATA_SERVICE(service), username, password, NULL, error)) {
    g_object_unref (service);
    return NULL;
  }
  return service;
}

static GDataFeed*
get_list_of_calendars(GDataCalendarService *service, GError **error)
{
  GDataFeed *feed = NULL;
  feed = gdata_calendar_service_query_all_calendars (service, NULL, NULL, NULL, NULL, error);
  return feed;
}

static GDataFeed*
get_list_of_events(GDataCalendarService *service,
                   GDataCalendarCalendar *calendar,
                   GError **error)
{
  GDataFeed *feed = NULL;
  feed = gdata_calendar_service_query_events (service, calendar, NULL, NULL, NULL, NULL, error);
  return feed;  
}

static void
output_calendar(gpointer data, gpointer user_data)
{
  GDataEntry *entry = GDATA_ENTRY (data);
  g_print("Calendar: %s (%s) id %s\n", gdata_entry_get_title (entry),
    gdata_entry_get_summary (entry), gdata_entry_get_id (entry));
} 

static void
check_event_difference(GDataCalendarEvent *gcevent, CEvent *mcevent)
{
  string mctitle;
  const gchar* gctitle;

  gctitle = gdata_entry_get_title (GDATA_ENTRY(gcevent));
  mctitle = mcevent->getSummary();
  if (strcmp (gctitle, mctitle.c_str()) != 0) {
    g_print("event mapped in gcal has different title: gcal: %s mcal: %s\n", gctitle, mctitle.c_str());
  }
}

static void
add_event(gpointer data, gpointer user_data)
{
  GDataCalendarEvent *gcevent = GDATA_CALENDAR_EVENT (data);
  struct shared_info* info = (struct shared_info*)user_data;
  CEvent *event = NULL;
  time_t start_time, end_time;
  GDataGDWhen *t;
  GList *places;
  GTimeVal start, end;
  int error;
  gpointer eventid;
  /*GList *times = gdata_calendar_event_get_times(event);
  for (times = times; times != NULL; times = times->next) {
    GDataGDWhen *t = GDATA_GD_WHEN(times->data);
    GTimeVal start, end;
    gchar *st_str, *en_str;
    gdata_gd_when_get_start_time (t, &start);
    gdata_gd_when_get_end_time (t, &end);
    st_str = g_time_val_to_iso8601 (&start);
    en_str = g_time_val_to_iso8601 (&end);
    g_free(st_str);
    g_print("Start time: %s End time: %s\n", st_str, en_str);
    g_free(en_str);
    start_time = start.tv_sec;
    end_time = end.tv_sec;
  }*/
  /*places = gdata_calendar_event_get_places(gcevent);
  if (places)
    place = (gchar*)gdata_gd_where_get_value_string(GDATA_GD_WHERE(places->data));*/

  gdata_calendar_event_get_primary_time (gcevent, &start, &end, &t);
  /*gdata_gd_when_get_start_time (t, &start);
  gdata_gd_when_get_end_time (t, &end);*/
  start_time = start.tv_sec;
  end_time = end.tv_sec;
  if (eventid = g_hash_table_lookup (info->event_mappings, gdata_entry_get_id (GDATA_ENTRY(gcevent)))) {
    event = info->mcal->getEvent((const char*)eventid, error);
    if (!event) {
      g_print("Event %s could not be found but is mapped as %s\n", gdata_entry_get_id (GDATA_ENTRY(gcevent)), eventid);
    }
    else {
      check_event_difference (gcevent, event);
    }
  }
  else {
    bool addeventret = false;    
    g_print("%s: %s...%s..%s\n", gdata_calendar_event_get_uid(gcevent), gdata_entry_get_title (GDATA_ENTRY(gcevent)), gdata_entry_get_content (GDATA_ENTRY (gcevent)));
    /* FIXME: need to do recurring events and places correctly */
    g_print("Adding new event\n");
    event = new CEvent(gdata_entry_get_title (GDATA_ENTRY (gcevent)), gdata_entry_get_content (GDATA_ENTRY (gcevent)) ? gdata_entry_get_content (GDATA_ENTRY (gcevent)) : "", "", start_time, end_time);
    addeventret = info->mcal->addEvent(event, error);
    if (addeventret && strcmp(event->getId().c_str(), "") != 0) {
      add_event_mapping(info->db, gdata_entry_get_id (GDATA_ENTRY (gcevent)), event->getId());
    }
    else {
      g_print("Error adding event: %s with title: %s. Error code: %d\n", gdata_calendar_event_get_uid(gcevent), gdata_entry_get_title (GDATA_ENTRY(gcevent)), error);
    }
  }
}

static void
add_calendar(gpointer data, gpointer user_data)
{
  GDataCalendarCalendar *calendar = GDATA_CALENDAR_CALENDAR (data);
  struct shared_info *info = (struct shared_info*)user_data;
  
  CCalendar *retval = NULL;
  GDataFeed *eventlistfeed;
  GError *gcerror;
  int error;
  gpointer calid;
  GList *entries = NULL;
  CMulticalendar *mc = CMulticalendar::MCInstance();

  GDataEntry *entry = GDATA_ENTRY (calendar);
  const gchar* gcalid = gdata_entry_get_id (entry);

  if (calid = g_hash_table_lookup (info->cal_mappings, gcalid)) {
    g_print("Calendar already been mapped\n");
    retval = mc->getCalendarById(atoi((gchar*)calid), error);
  }
  else {
    retval = new CCalendar(gdata_entry_get_title (entry), COLOUR_NEXT_FREE, FALSE, TRUE, SYNC_CALENDAR, "SomeTune.xyz",
      "Version-1.0");
    mc->addCalendar(retval, error);
    add_calendar_mapping (info, gdata_entry_get_id (entry), retval->getCalendarId());
  }  
  eventlistfeed = get_list_of_events (info->service, calendar, &gcerror);
  //if (!eventlistfeed) goto cleanup;
  entries = gdata_feed_get_entries (eventlistfeed);
  //if (!entries) goto cleanup;
  info->mcal = retval;
  g_list_foreach (entries, add_event, info);
}


sqlite3* connect_to_database(const char *filename)
{
  sqlite3* retval;
  sqlite3_open(filename, &retval);
  return retval;
}

void add_event_mapping(sqlite3* db, const gchar* gceventid, const string& meventid)
{
  int ret;
  char **tables;
  int rows, cols;
  char *errmsg;
  sqlite3_stmt *statement;

  sqlite3_prepare_v2 (db, "INSERT INTO event_mapping VALUES (?, ?)", -1, &statement, NULL);
  sqlite3_bind_text (statement, 1, gceventid, -1, SQLITE_STATIC);
  sqlite3_bind_text (statement, 2, meventid.c_str(), -1, SQLITE_STATIC);
  ret = sqlite3_step (statement);
  if (ret == SQLITE_DONE) {
    sqlite3_finalize (statement);
  }
  else {
    g_print("Error: %d\n", ret);
  }
}

void add_calendar_mapping(struct shared_info *info, const gchar* gcalid, int mcalid)
{
  int ret;
  char **tables;
  int rows, cols;
  char *errmsg;
  sqlite3_stmt *statement;
  sqlite3_prepare_v2 (info->db, "INSERT INTO calendar_mapping VALUES (?, ?)", -1, &statement, NULL);
  sqlite3_bind_text (statement, 1, gcalid, -1, SQLITE_STATIC);
  sqlite3_bind_int (statement, 2, mcalid);
  ret = sqlite3_step (statement);
  if (ret == SQLITE_DONE) {
    sqlite3_finalize (statement);
  }
  else {
    g_print("Error: %d\n", ret);
  }
}

void add_account(struct shared_info *info, const gchar* username, const gchar* password)
{
  int ret;
  sqlite3_stmt *statement;

  sqlite3_prepare_v2 (info->db, "INSERT INTO google_accounts VALUES (?, ?)", -1, &statement, NULL);
  sqlite3_bind_text (statement, 1, username, -1, SQLITE_STATIC);
  sqlite3_bind_text (statement, 2, password, -1, SQLITE_STATIC);
  ret = sqlite3_step (statement);
  if (ret == SQLITE_DONE) {
    sqlite3_finalize (statement);
    g_hash_table_insert (info->accounts, g_strdup(username), g_strdup(password));
  }
  else {
    g_print("Error: %d\n", ret);
  }
}

void add_calendar_forsync(struct shared_info *info, const gchar* calid, int forsync)
{
  int ret;
  sqlite3_stmt *statement;

  sqlite3_prepare_v2 (info->db, "INSERT INTO google_calendars VALUES (?, ?)", -1, &statement, NULL);
  sqlite3_bind_text (statement, 1, calid, -1, SQLITE_STATIC);
  sqlite3_bind_int (statement, 2, forsync);
  ret = sqlite3_step (statement);
  if (ret == SQLITE_DONE) {
    sqlite3_finalize (statement);
    g_hash_table_insert (info->forsync, (gpointer)calid, (gpointer)forsync);
  }
  else {
    g_print("Error: %d\n", ret);
  }
}

void initialise_database(sqlite3* db)
{
  char** tables;
  char* errmsg;
  int rows, cols;
  sqlite3_get_table(db, "SELECT * from sqlite_master", &tables, &rows, &cols, &errmsg);
  sqlite3_free_table (tables);
  if (rows == 0) {
    sqlite3_get_table (db, "CREATE TABLE calendar_mapping (gcalid string, mcalid string)", &tables, &rows, &cols, &errmsg);
    if (errmsg) goto error;
    
    sqlite3_free_table (tables);
    sqlite3_get_table (db, "CREATE TABLE event_mapping (geventid string, meventid string)", &tables, &rows, &cols, &errmsg);
    if (errmsg) goto error;
    sqlite3_free_table (tables);
    sqlite3_get_table (db, "CREATE TABLE google_calendars (gcalid string, forsync boolean)", &tables, &rows, &cols, &errmsg);
    if (errmsg) goto error;
    sqlite3_get_table (db, "CREATE TABLE google_accounts (googleac string, password string)", &tables, &rows, &cols, &errmsg);
    if (errmsg) goto error;
  }
  return;
error:
  g_print("Error: %s\n", errmsg);
  if (tables) sqlite3_free_table (tables);
  close_database(db);
  exit(1);
}

void close_database(sqlite3* db)
{
  sqlite3_close (db);
}

void get_data(struct shared_info* info)
{
  char **tables;
  int rows, cols;
  char *errmsg;
  int i;

  sqlite3_get_table (info->db, "SELECT * from calendar_mapping", &tables, &rows, &cols, &errmsg);
  if (errmsg) goto error;

  for(i=0;i<rows;i++) {
    g_hash_table_insert (info->cal_mappings, g_strdup(tables[cols + i*cols]), g_strdup(tables[cols + i*cols + 1]));
    g_print("Calendar mapping of %s to %s\n", tables[cols + i*cols], tables[cols + i*cols + 1]);
  }

  sqlite3_free_table(tables);

  sqlite3_get_table (info->db, "SELECT * from event_mapping", &tables, &rows, &cols, &errmsg);
  if (errmsg) goto error;

  for(i=0;i<rows;i++) {
    g_hash_table_insert (info->event_mappings, g_strdup(tables[cols + i*cols]), g_strdup(tables[cols + i*cols + 1]));
    g_print("Event mapping of %s to %s\n", tables[cols + i*cols], tables[cols + i*cols + 1]);

  }
  sqlite3_free_table(tables);

  sqlite3_get_table (info->db, "SELECT * from google_calendars", &tables, &rows, &cols, &errmsg);
  if (errmsg) goto error;

  for(i=0;i<rows;i++) {
    g_hash_table_insert (info->forsync, g_strdup(tables[cols + i*cols]), (gpointer)atoi(tables[cols + i*cols + 1]));
  }
  sqlite3_free_table (tables);

  sqlite3_get_table (info->db, "SELECT * from google_accounts", &tables, &rows, &cols, &errmsg);
  if (errmsg) goto error;

  for(i=0;i<rows;i++) {
    g_hash_table_insert (info->accounts, g_strdup(tables[cols + i*cols]), g_strdup(tables[cols + i*cols + 1]));
  }
  sqlite3_free_table (tables);
  return;
error:
  g_print("Error: %s\n", errmsg);
  if (tables) sqlite3_free_table (tables);
  close_database(info->db);
  exit(1);
}

void
sync_calendars_for_account(gpointer key, gpointer value, gpointer user_data)
{
  struct shared_info *info = (struct shared_info*)user_data;
  gchar* username = (gchar*)key;
  gchar* password = (gchar*)value;
  GDataCalendarService *service;
  GDataFeed *callistfeed, *eventlistfeed;
  GError *error = NULL;
  GTimeVal start;
  GTimeVal end;
  GList *entries, *entry;

  callistfeed = NULL;
  eventlistfeed = NULL;

  g_print("logging into gcal with account %s:%s\n", username, password);

  service = login_to_gcal (username, password, &error);
  info->googleac = username;
  info->service = service;

  if (!service) goto cleanup;
  g_print("logged into gcal with account %s\n", username);
  callistfeed = get_list_of_calendars(service, &error);
  entries = gdata_feed_get_entries (callistfeed);
  g_list_foreach (entries, add_calendar, info);
  if (!entries) goto cleanup;

cleanup:
  if (error) {
    g_print("Error: %s\n", error->message);
    g_error_free (error);
  }
  if (callistfeed) g_object_unref (callistfeed);
  if (eventlistfeed) g_object_unref (eventlistfeed);

}

void
sync_calendars(struct shared_info *info, int argc, char** argv)
{
  int i;

  if (g_hash_table_size (info->accounts) == 0) {
    if (argc >= 3) {
      add_account (info, argv[1], argv[2]);
    }
    else {
      g_print ("No Google accounts stored and none provided on command line\n");
      return;
    }    
  }

  g_hash_table_foreach (info->accounts, sync_calendars_for_account, info);
}

int main(int argc, char** argv)
{
  GDataCalendarQuery *query;
  GDataCalendarCalendar *firstcal = NULL;
  CMulticalendar *mc = CMulticalendar::MCInstance();
  struct shared_info info;

  g_thread_init(NULL);
  g_type_init();

  info.cal_mappings = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
  info.event_mappings = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
  info.accounts = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
  info.forsync = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

  info.db = connect_to_database("/home/user/MyDocs/mgcalsync.db");
  initialise_database(info.db);
  get_data(&info);

  sync_calendars(&info, argc, argv);
    
  g_hash_table_destroy (info.cal_mappings);
  g_hash_table_destroy (info.event_mappings);
  g_hash_table_destroy (info.accounts);
  g_hash_table_destroy (info.forsync);
  close_database(info.db);
}
