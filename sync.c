/* sync.c
 * 
 * Copyright (C) 1999 by Judd Montgomery
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "config.h"
#include "i18n.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/types.h>
#include <dirent.h>
#include <sys/wait.h>
#include <sys/stat.h>
#ifdef USE_FLOCK
#include <sys/file.h>
#else
#include <fcntl.h>
#endif
#include <errno.h>
#include <signal.h>
#include <utime.h>
#include <stdio.h>
#include <ctype.h>
#include "utils.h"
#include "sync.h"
#include "japanese.h"
#include "cp1250.h"
#include "russian.h"
#include "log.h"
#include "prefs.h"
#include "datebook.h"
#include "plugins.h"
#include "libplugin.h"
#include "password.h"

/*#include <pi-source.h> */
#include <pi-socket.h>
#include <pi-dlp.h>
#include <pi-file.h>
#include <pi-version.h>

/* #define JPILOT_DEBUG */

/* This struct is copied from pi-file.c
 * The only reason I have it here is for the Graffiti Shortcut hack.
 * Otherwise, it shouldn't be duplicated here.
 */
struct pi_file {
   int err;
   int for_writing;
   FILE *f;
   FILE *tmpf;
   char *file_name;
   
   struct DBInfo info;
   int app_info_size;
   void *app_info;
   int sort_info_size;
   void *sort_info;
   int next_record_list_id;
   int resource_flag;
   int ent_hdr_size;
  
   unsigned long unique_id_seed;
   int nentries;
   int nentries_allocated;
   struct pi_file_entry *entries;
   
   void *rbuf;
   int rbuf_size;
};

extern int pipe_in, pipe_out;
extern pid_t glob_child_pid;

int slow_sync_application(char *DB_name, int sd);
int fast_sync_application(char *DB_name, int sd);
int sync_fetch(int sd, unsigned int flags, const int num_backups, int fast_sync);
int jpilot_sync(struct my_sync_info *sync_info);
int sync_lock();
int sync_unlock();
static int sync_process_install_file(int sd);
static int sync_rotate_backups(const int num_backups);
int pdb_file_modify_record(char *DB_name, void *record_in, int size_in,
			   int attr_in, int cat_in, pi_uid_t uid_in);
int pdb_file_read_record_by_id(char *DB_name, 
			       pi_uid_t uid,
			       void **bufp, int *sizep, int *idxp,
			       int *attrp, int * catp);

int pdb_file_delete_record_by_id(char *DB_name, pi_uid_t uid_in);


void recode_packed_record(char *DB_name, void *record, int rec_len, long char_seet);


void sig_handler(int sig)
{
   jpilot_logf(LOG_DEBUG, "caught signal SIGCHLD\n");
   glob_child_pid = 0;

   /*wait for any child processes */
   waitpid(-1, NULL, WNOHANG);

   /*refresh the screen after a sync */
   /*cb_app_button(NULL, GINT_TO_POINTER(REDRAW));*/

   return;
}

static int writef(int fp, char *format, ...)
{
#define WRITE_MAX_BUF	4096
   va_list	       	val;
   char			buf[WRITE_MAX_BUF];

   buf[0] = '\0';

   va_start(val, format);
   g_vsnprintf(buf, WRITE_MAX_BUF ,format, val);
   /*just in case g_vsnprintf reached the max */
   buf[WRITE_MAX_BUF-1] = 0;
   va_end(val);

   write(fp, buf, strlen(buf));

   return TRUE;
}

#ifdef USE_LOCKING
int sync_lock(int *fd)
{
   pid_t pid;
   char lock_file[256];
   int r;
   char str[12];
#ifndef USE_FLOCK
   struct flock lock;
#endif

   get_home_file_name("sync_pid", lock_file, 255);
   *fd = open(lock_file, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
   if (*fd<0) {
      jpilot_logf(LOG_WARN, "open lock file failed\n");
      return -1;
   }
#ifndef USE_FLOCK
   lock.l_type = F_WRLCK;
   lock.l_start = 0;
   lock.l_whence = SEEK_SET;
   lock.l_len = 0; /*Lock to the end of file */
   r = fcntl(*fd, F_SETLK, &lock);
#else
   r = flock(*fd, LOCK_EX | LOCK_NB);
#endif
   if (r == -1){
      jpilot_logf(LOG_WARN, "lock failed\n");
      read(*fd, str, 10);
      pid = atoi(str);
      jpilot_logf(LOG_FATAL, "sync file is locked by pid %d\n", pid);
      return -1;
   } else {
      jpilot_logf(LOG_DEBUG, "lock succeeded\n");
      pid=getpid();
      sprintf(str, "%d\n", pid);
      write(*fd, str, strlen(str)+1);
      ftruncate(*fd, strlen(str)+1);
   }
   return 0;
}

int sync_unlock(int fd)
{
   pid_t pid;
   char lock_file[256];
   int r;
   char str[12];
#ifndef USE_FLOCK
   struct flock lock;
#endif

   get_home_file_name("sync_pid", lock_file, 255);

#ifndef USE_FLOCK
   lock.l_type = F_UNLCK;
   lock.l_start = 0;
   lock.l_whence = SEEK_SET;
   lock.l_len = 0;
   r = fcntl(fd, F_SETLK, &lock);
#else
   r = flock(fd, LOCK_UN | LOCK_NB);
#endif
   if (r == -1) {
      jpilot_logf(LOG_WARN, "unlock failed\n");
      read(fd, str, 10);
      pid = atoi(str);
      jpilot_logf(LOG_WARN, "sync is locked by pid %d\n", pid);
      close(fd);
      return -1;
   } else {
      jpilot_logf(LOG_DEBUG, "unlock succeeded\n");
      ftruncate(fd, 0);
      close(fd);
   }
   return 0;
}
#endif

int sync_once(struct my_sync_info *sync_info)
{
#ifdef USE_LOCKING
   int fd;
#endif
   int r;
   struct my_sync_info *sync_info_copy;

   if (glob_child_pid) {
      jpilot_logf(LOG_WARN, PN": sync PID = %d\n", glob_child_pid);
      jpilot_logf(LOG_WARN, PN": press the hotsync button on the cradle "
	   "or \"kill %d\"\n", glob_child_pid);
      return 0;
   }

   /* Make a copy of the sync info for the forked process */
   sync_info_copy = malloc(sizeof(struct my_sync_info));
   if (!sync_info_copy) {
      jpilot_logf(LOG_WARN, PN":sync_once(): Out of memory\n");
      return 0;
   }
   memcpy(sync_info_copy, sync_info, sizeof(struct my_sync_info));

   if (!(sync_info->flags & SYNC_NO_FORK)) {
      jpilot_logf(LOG_DEBUG, "forking sync process\n");
      switch ( glob_child_pid = fork() ){
       case -1:
	 perror("fork");
	 return 0;
       case 0:
	 /*close(pipe_in); */
	 break;
       default:
	 signal(SIGCHLD, sig_handler);
	 return 0;
      }
   }
#ifdef USE_LOCKING
   r = sync_lock(&fd);
   if (r) {
      jpilot_logf(LOG_DEBUG, "Child cannot lock file\n");
      free(sync_info_copy);
      _exit(0);
   }
#endif

   r = jpilot_sync(sync_info_copy);
   if (r) {
      writef(pipe_out, "exiting with status %d\n", r);
   }
#ifdef USE_LOCKING
   sync_unlock(fd);
#endif
   jpilot_logf(LOG_DEBUG, "sync child exiting\n");
   free(sync_info_copy);
   if (!(sync_info->flags & SYNC_NO_FORK)) {
      _exit(0);
   } else {
      return r;
   }
}

static void filename_make_legal(char *s)
{
   char *p;
   for (p=s; *p; p++) {
      if (*p=='/') {
	 *p='?';
      }
   }
}

int jpilot_sync(struct my_sync_info *sync_info)
{
   struct pi_sockaddr addr;
   int sd;
   int ret;
   struct PilotUser U;
   const char *device;
   char default_device[]="/dev/pilot";
   int found=0, fast_sync=0;
   int i;
   int dev_usb;
   char link[256], dev_str[256], dev_dir[256], *Pc;
#ifdef ENABLE_PLUGINS
   GList *plugin_list, *temp_list;
   struct plugin_s *plugin;
#endif
#ifdef JPILOT_DEBUG
   int start;
   struct DBInfo info;
#endif
#ifdef ENABLE_PRIVATE
   char hex_password[PASSWD_LEN*2+4];
#endif
   char buf[1024];
   long char_set;
   
   device = NULL;
   if (sync_info->port) {
      if (sync_info->port[0]) {
	 /*A port was passed in to use */
	 device=sync_info->port;
	 found = 1;
      }
   }
   if (!found) {
      /*No port was passed in, look in env */
      device = getenv("PILOTPORT");
      if (device == NULL) {
	 device = default_device;
      }
   }

#ifdef ENABLE_PLUGINS
   if (!(sync_info->flags & SYNC_NO_PLUGINS)) {
      jpilot_logf(LOG_DEBUG, "sync:calling load_plugins\n");
      load_plugins();
   }
     
   /* Do the pre_sync plugin calls */
   plugin_list=NULL;

   plugin_list = get_plugin_list();

   for (temp_list = plugin_list; temp_list; temp_list = temp_list->next) {
      plugin = (struct plugin_s *)temp_list->data;
      if (plugin) {
	 if (plugin->sync_on) {
	    if (plugin->plugin_pre_sync) {
	       jpilot_logf(LOG_DEBUG, "sync:calling plugin_pre_sync for [%s]\n", plugin->name);
	       plugin->plugin_pre_sync();
	    }
	 }
      }
   }
#endif
   
   writef(pipe_out, "****************************************\n");
   writef(pipe_out, " Syncing on device %s\n", device);
   writef(pipe_out, _(" Press the HotSync button now\n"));
   writef(pipe_out, "****************************************\n");
   
   if (!(sd = pi_socket(PI_AF_SLP, PI_SOCK_STREAM, PI_PF_PADP))) {
      perror("pi_socket");
      writef(pipe_out, "pi_socket %s\n", strerror(errno));
#ifdef ENABLE_PLUGINS
      free_plugin_list(&plugin_list);
#endif
      return -1;
   }
    
   addr.pi_family = PI_AF_SLP;
   strcpy(addr.pi_device, device);
  
   /* This is for USB, whose device doesn't exist until the cradle is pressed
    * We will give them 5 seconds */
   dev_str[0]='\0';
   link[0]='\0';
   strncpy(dev_str, device, 255);
   strncpy(dev_dir, device, 255);
   dev_str[255]='0';
   dev_dir[255]='0';
   dev_usb=0;
   for (Pc=&dev_dir[strlen(dev_dir)-1]; Pc>dev_dir; Pc--) {
      if (*Pc=='/') *Pc='\0';
      else break;
   }
   Pc = strrchr(dev_dir, '/');
   if (Pc) {
      *Pc = '\0';
   }
   for (i=10; i>0; i--) {
      ret = readlink(dev_str, link, 256);
      if (ret>0) {
	 link[ret]='\0';
      } else {
	 break;
      }
      if (link[0]=='/') {
	 strcpy(dev_str, link);
	 strcpy(dev_dir, link);
	 for (Pc=&dev_dir[strlen(dev_dir)-1]; Pc>dev_dir; Pc--) {
	    if (*Pc=='/') *Pc='\0';
	    else break;
	 }
	 Pc = strrchr(dev_dir, '/');
	 if (Pc) {
	    *Pc = '\0';
	 }
      } else {
	 g_snprintf(dev_str, 255, "%s/%s", dev_dir, link);
	 dev_str[255]='\0';
      }
      if (strstr(link, "usb")) {
	 dev_usb=1;
	 break;
      }
   }
   if (dev_usb) {
      for (i=5; i>0; i--) {
	 ret = pi_bind(sd, (struct sockaddr*)&addr, sizeof(addr));
	 if (ret!=-1) break;
	 sleep(1);
      }
   } else {
      ret = pi_bind(sd, (struct sockaddr*)&addr, sizeof(addr));
   }
   if (ret == -1) {
      perror("pi_bind");
      writef(pipe_out, "pi_bind %s\n", strerror(errno));
      writef(pipe_out, _("Check your serial port and settings\n"));
#ifdef ENABLE_PLUGINS
      free_plugin_list(&plugin_list);
#endif
      return SYNC_ERROR_BIND;
   }

   ret = pi_listen(sd,1);
   if(ret == -1) {
      perror("pi_listen");
      writef(pipe_out, "pi_listen %s\n", strerror(errno));
#ifdef ENABLE_PLUGINS
      free_plugin_list(&plugin_list);
#endif
      return SYNC_ERROR_LISTEN;
   }

   sd = pi_accept(sd, 0, 0);
   if(sd == -1) {
      perror("pi_accept");
      writef(pipe_out, "pi_accept %s\n", strerror(errno));
#ifdef ENABLE_PLUGINS
      free_plugin_list(&plugin_list);
#endif
      return SYNC_ERROR_PI_ACCEPT;
   }

   dlp_ReadUserInfo(sd, &U);

   /* Do some checks to see if this is the same palm that was synced
    * the last time
    */
   if (U.userID == 0) {
      writef(pipe_out, "Last Username-->\"%s\"\n", sync_info->username);
      writef(pipe_out, "Last UserID-->\"%d\"\n", sync_info->userID);
      writef(pipe_out, "Username-->\"%s\"\n", U.username);
      writef(pipe_out, "User ID-->%d\n", U.userID);
      dlp_EndOfSync(sd, 0);
      pi_close(sd);
      return SYNC_ERROR_NULL_USERID;
   }
   if ((sync_info->userID != U.userID) &&
       (sync_info->userID != 0) &&
       (!(sync_info->flags & SYNC_OVERRIDE_USER))) {
      /* These are carefully worded so as not to be read by
       * the parent program and interpreted */
      writef(pipe_out, "Last Username-->\"%s\"\n", sync_info->username);
      writef(pipe_out, "Last UserID-->\"%d\"\n", sync_info->userID);
      writef(pipe_out, "Username-->\"%s\"\n", U.username);
      writef(pipe_out, "User ID-->%d\n", U.userID);
      dlp_EndOfSync(sd, 0);
      pi_close(sd);
      return SYNC_ERROR_NOT_SAME_USERID;
   }
   if ((strcmp(sync_info->username, U.username)) &&
       (sync_info->username[0]!='\0') &&
       (!(sync_info->flags & SYNC_OVERRIDE_USER))) {
      writef(pipe_out, "Last Username-->\"%s\"\n", sync_info->username);
      writef(pipe_out, "Last UserID-->\"%d\"\n", sync_info->userID);
      writef(pipe_out, "Username-->\"%s\"\n", U.username);
      writef(pipe_out, "User ID-->%d\n", U.userID);
      dlp_EndOfSync(sd, 0);
      pi_close(sd);
      return SYNC_ERROR_NOT_SAME_USER;
   }
   
   /* User name and User ID is read by the parent process and stored
    * in the preferences
    * So, this is more than just displaying it to the user */
   writef(pipe_out, "Username is \"%s\"\n", U.username);
   writef(pipe_out, "User ID is %d\n", U.userID);
   jpilot_logf(LOG_DEBUG, "Last Username = [%s]\n", sync_info->username);
   jpilot_logf(LOG_DEBUG, "Last UserID = %d\n", sync_info->userID);
   jpilot_logf(LOG_DEBUG, "Username = [%s]\n", U.username);
   jpilot_logf(LOG_DEBUG, "userID = %d\n", U.userID);
   jpilot_logf(LOG_DEBUG, "lastSyncPC = %d\n", U.lastSyncPC);
  
   writef(pipe_out, "lastSyncPC = %d\n", U.lastSyncPC);
   writef(pipe_out, "This PC = %d\n", sync_info->PC_ID);
#ifdef ENABLE_PRIVATE
   if (U.passwordLength > 0) {
      bin_to_hex_str(U.password, hex_password, PASSWD_LEN);
   } else {
      strcpy(hex_password, "09021345070413440c08135a3215135dd217ead3b5df556322e9a14a994b0f88");
   }
   jpilot_logf(LOG_DEBUG, "userPassword = [%s]\n", hex_password);
   writef(pipe_out, "User Password is \"%s\"\n", hex_password);
#endif

   if (dlp_OpenConduit(sd)<0) {
      writef(pipe_out, "Sync canceled\n");
#ifdef ENABLE_PLUGINS
      free_plugin_list(&plugin_list);
#endif
      dlp_EndOfSync(sd, 0);
      pi_close(sd);
      return SYNC_ERROR_OPEN_CONDUIT;
   }

   sync_process_install_file(sd);

#ifdef JPILOT_DEBUG
   start=0;
   while(dlp_ReadDBList(sd, 0, dlpOpenRead, start, &info)>0) {
      start=info.index+1;
      if (info.flags & dlpDBFlagAppInfoDirty) {
	 printf("appinfo dirty for %s\n", info.name);
      }
   }
#endif

   if ( (!(sync_info->flags & SYNC_OVERRIDE_USER)) &&
       (U.lastSyncPC == sync_info->PC_ID) ) {
      fast_sync=1;
      writef(pipe_out, _("Doing a fast sync.\n"));
      if (get_pref_int_default(PREF_SYNC_DATEBOOK, 1)) {
	 fast_sync_application("DatebookDB", sd);
      }
      if (get_pref_int_default(PREF_SYNC_ADDRESS, 1)) {
	 fast_sync_application("AddressDB", sd);
      }
      if (get_pref_int_default(PREF_SYNC_TODO, 1)) {
	 fast_sync_application("ToDoDB", sd);
      }
      if (get_pref_int_default(PREF_SYNC_MEMO, 1)) {
	 fast_sync_application("MemoDB", sd);
      }
   } else {
      fast_sync=0;
      writef(pipe_out, _("Doing a slow sync.\n"));
      if (get_pref_int_default(PREF_SYNC_DATEBOOK, 1)) {
	 slow_sync_application("DatebookDB", sd);
      }
      if (get_pref_int_default(PREF_SYNC_ADDRESS, 1)) {
	 slow_sync_application("AddressDB", sd);
      }
      if (get_pref_int_default(PREF_SYNC_TODO, 1)) {
	 slow_sync_application("ToDoDB", sd);
      }
      if (get_pref_int_default(PREF_SYNC_MEMO, 1)) {
	 slow_sync_application("MemoDB", sd);
      }
   }
   
   
#ifdef ENABLE_PLUGINS
   plugin_list = get_plugin_list();

   for (temp_list = plugin_list; temp_list; temp_list = temp_list->next) {
      plugin = (struct plugin_s *)temp_list->data;
      jpilot_logf(LOG_DEBUG, "syncing plugin name: [%s]\n", plugin->name);
      jpilot_logf(LOG_DEBUG, "syncing plugin DB:   [%s]\n", plugin->db_name);
      if (fast_sync) {
	 if (plugin->sync_on) {
	    fast_sync_application(plugin->db_name, sd);
	 }
      } else {
	 if (plugin->sync_on) {
	    slow_sync_application(plugin->db_name, sd);
	 }
      }
   }
#endif

#ifdef ENABLE_PLUGINS
   /* Do the sync plugin calls */
   plugin_list=NULL;

   plugin_list = get_plugin_list();

   for (temp_list = plugin_list; temp_list; temp_list = temp_list->next) {
      plugin = (struct plugin_s *)temp_list->data;
      if (plugin) {
	 if (plugin->sync_on) {
	    if (plugin->plugin_sync) {
	       jpilot_logf(LOG_DEBUG, "calling plugin_sync for [%s]\n", plugin->name);
	       plugin->plugin_sync(sd);
	    }
	 }
      }
   }
#endif
   
   sync_fetch(sd, sync_info->flags, sync_info->num_backups, fast_sync);
   
   /* Tell the user who it is, with this PC id. */
   U.lastSyncPC = sync_info->PC_ID;
   U.successfulSyncDate = time(NULL);
   U.lastSyncDate = U.successfulSyncDate;
   dlp_WriteUserInfo(sd, &U);
   if (strncpy(buf,_("Thank you for using J-Pilot."),1024) == NULL) {
      jpilot_logf(LOG_DEBUG, "memory allocation internal error\n");
      dlp_EndOfSync(sd, 0);
      pi_close(sd);
#ifdef ENABLE_PLUGINS
      free_plugin_list(&plugin_list);
#endif
      return 0;
   }
   get_pref(PREF_CHAR_SET, &char_set, NULL);
   if (char_set == CHAR_SET_JAPANESE) Euc2Sjis(buf, 1023);
   if (char_set == CHAR_SET_1250) Lat2Win(buf, 1023);
   if (char_set == CHAR_SET_1251) koi8_to_win1251(buf, 1023);
   if (char_set == CHAR_SET_1251_B) win1251_to_koi8(buf, 1023);
   dlp_AddSyncLogEntry(sd, buf);
   dlp_AddSyncLogEntry(sd, "\n\r");

   dlp_EndOfSync(sd, 0);
   pi_close(sd);

   cleanup_pc_files();

#ifdef ENABLE_PLUGINS
   /* Do the sync plugin calls */
   plugin_list=NULL;

   plugin_list = get_plugin_list();

   for (temp_list = plugin_list; temp_list; temp_list = temp_list->next) {
      plugin = (struct plugin_s *)temp_list->data;
      if (plugin) {
	 if (plugin->sync_on) {
	    if (plugin->plugin_post_sync) {
	       jpilot_logf(LOG_DEBUG, "calling plugin_post_sync for [%s]\n", plugin->name);
	       plugin->plugin_post_sync();
	    }
	 }
      }
   }

   jpilot_logf(LOG_DEBUG, "freeing plugin list\n");
   free_plugin_list(&plugin_list);
#endif
   
   writef(pipe_out, _("Finished.\n"));

   return 0;
}

int slow_sync_application(char *DB_name, int sd)
{
   unsigned long new_id;
   int db;
   int ret;
   int num;
   FILE *pc_in;
   PC3RecordHeader header;
   char *record;
   char pronoun[10];
   int rec_len;
   char pc_filename[256];
   char write_log_message[256];
   char error_log_message_w[256];
   char error_log_message_d[256];
   char delete_log_message[256];
   char log_entry[256];
   /* recordid_t id=0; */
   int index, size, attr, category;
   char buffer[65536];
   long char_set;

   if ((DB_name==NULL) || (strlen(DB_name) == 0) || (strlen(DB_name) > 250)) {
      return -1;
   }
   g_snprintf(log_entry, 255, "Syncing %s\n", DB_name);
   log_entry[255]='\0';
   writef(pipe_out, log_entry);
   g_snprintf(pc_filename, 255, "%s.pc3", DB_name);
   /* This is an attempt to use the proper pronoun most of the time */
   if (strchr("aeiou", tolower(DB_name[0]))) {
      strcpy(pronoun, "an");
   } else {
      strcpy(pronoun, "a");
   }
   g_snprintf(write_log_message, 255,
	      "Wrote %s %s record.\n\r", pronoun, DB_name);
   g_snprintf(error_log_message_w, 255,
	      "Writing %s %s record failed.\n\r", pronoun, DB_name);
   g_snprintf(error_log_message_d, 255,
	      "Deleting %s %s record failed.\n\r", pronoun, DB_name);
   g_snprintf(delete_log_message, 256,
	      "Deleted %s %s record.\n\r", pronoun, DB_name);

   pc_in = jp_open_home_file(pc_filename, "r+");
   if (pc_in==NULL) {
      writef(pipe_out, "Unable to open %s\n",pc_filename);
      return -1;
   }
   /* Open the applications database, store access handle in db */
   if (dlp_OpenDB(sd, 0, dlpOpenReadWrite, DB_name, &db) < 0) {
      g_snprintf(log_entry, 255, "Unable to open %s\n\r", DB_name);
      log_entry[255]='\0';
      dlp_AddSyncLogEntry(sd, log_entry);
      return -1;
   }

#ifdef JPILOT_DEBUG
   dlp_ReadOpenDBInfo(sd, db, &num);
   writef(pipe_out ,"number of records = %d\n", num);
#endif
   while(!feof(pc_in)) {
      num = read_header(pc_in, &header);
      if (num!=1) {
	 if (ferror(pc_in)) {
	    break;
	 }
	 if (feof(pc_in)) {
	    break;
	 }
      }
      rec_len = header.rec_len;
      if (rec_len > 0x10000) {
	 writef(pipe_out, "PC file corrupt?\n");
	 fclose(pc_in);
	 return -1;
      }
      if (header.rt==NEW_PC_REC) {
	 record = malloc(rec_len);
	 if (!record) {
	    writef(pipe_out, "slow_sync_application(): Out of memory\n");
	    break;
	 }
	 num = fread(record, rec_len, 1, pc_in);
	 if (num != 1) {
	    if (ferror(pc_in)) {
	       break;
	    }
	 }

	 get_pref(PREF_CHAR_SET, &char_set, NULL);
	 if (char_set==CHAR_SET_JAPANESE ||
	     char_set==CHAR_SET_1250 ||
	     char_set==CHAR_SET_1251 ||
	     char_set==CHAR_SET_1251_B
	     ) {
	    recode_packed_record(DB_name, record, rec_len, char_set);
	 }

	 ret = dlp_WriteRecord(sd, db, header.attrib & dlpRecAttrSecret,
			       0, header.attrib & 0x0F,
			       record, rec_len, &new_id);
	 
	 if (record) {
	    free(record);
	    record = NULL;
	 }

	 if (ret < 0) {
	    writef(pipe_out, "dlp_WriteRecord failed\n");
	    dlp_AddSyncLogEntry(sd, error_log_message_w);
	 } else {
	    dlp_AddSyncLogEntry(sd, write_log_message);
	    /*Now mark the record as deleted in the pc file */
	    if (fseek(pc_in, -(header.header_len+rec_len), SEEK_CUR)) {
	       writef(pipe_out, "fseek failed - fatal error\n");
	       fclose(pc_in);
	       return -1;
	    }
	    header.rt=DELETED_PC_REC;
	    write_header(pc_in, &header);
	 }
      }

      if ((header.rt==DELETED_PALM_REC) || (header.rt==MODIFIED_PALM_REC)) {
	 rec_len = header.rec_len;
	 record = malloc(rec_len);
	 num = fread(record, rec_len, 1, pc_in);
	 if (num != 1) {
	    if (ferror(pc_in)) {
	       break;
	    }
	 }
	 if (fseek(pc_in, -rec_len, SEEK_CUR)) {
	    writef(pipe_out, "fseek failed - fatal error\n");
	    fclose(pc_in);
	    return -1;
	 }
	 ret = dlp_ReadRecordById(sd, db, header.unique_id, buffer,
				  &index, &size, &attr, &category);
	 /* printf("read record by id %s returned %d\n", DB_name, ret); */
	 /* printf("rec_len = %d size = %d\n", rec_len, size); */
	 if (rec_len == size) {
	    jpilot_logf(LOG_DEBUG, "sizes match!\n");
#ifdef JPILOT_DEBUG
	    if (memcmp(record, buffer, size)==0) {
	       jpilot_logf(LOG_DEBUG, "Binary is the same!\n");
	    }
#endif
	 }
	 /* if (ret>=0 ) {
	    printf("id %ld, index %d, size %d, attr 0x%x, category %d\n",id, index, size, attr, category);
	 }
	 writef(pipe_out, "Deleting Palm id=%d,\n",header.unique_id); */

	 ret = dlp_DeleteRecord(sd, db, 0, header.unique_id);
	 
	 if (ret < 0) {
	    writef(pipe_out, "dlp_DeleteRecord failed\n"\
            "This could be because the record was already deleted on the Palm\n");
	    dlp_AddSyncLogEntry(sd, error_log_message_d);
	 } else {
	    dlp_AddSyncLogEntry(sd, delete_log_message);
	 }
	 /*Now mark the record as deleted */
	 if (fseek(pc_in, -header.header_len, SEEK_CUR)) {
	    writef(pipe_out, "fseek failed - fatal error\n");
	    fclose(pc_in);
	    return -1;
	 }
	 header.rt=DELETED_DELETED_PALM_REC;
	 write_header(pc_in, &header);
      }

      /*skip this record now that we are done with it */
      if (fseek(pc_in, rec_len, SEEK_CUR)) {
	 writef(pipe_out, "fseek failed - fatal error\n");
	 fclose(pc_in);
	 return -1;
      }
   }
   fclose(pc_in);

#ifdef JPILOT_DEBUG
   dlp_ReadOpenDBInfo(sd, db, &num);
   writef(pipe_out ,"number of records = %d\n", num);
#endif

   dlp_ResetSyncFlags(sd, db);
   dlp_CleanUpDatabase(sd, db);

   /* Close the database */
   dlp_CloseDB(sd, db);

   return 0;
}

/*
 * Fetch the databases from the palm if modified
 */
int fetch_extra_DBs(int sd, char *palm_dbname[])
{
#define MAX_DBNAME 50
   struct pi_file *pi_fp;
   char full_name[256];
   struct stat statb;
   struct utimbuf times;
   int i;
   int found;
   int cardno, start;
   struct DBInfo info;
   char db_copy_name[MAX_DBNAME];
   
   start=cardno=0;
   
   while(dlp_ReadDBList(sd, cardno, dlpOpenRead, start, &info)>0) {
      start=info.index+1;
      found = 0;
      for (i=0; palm_dbname[i]; i++) {
	 if (palm_dbname[i]==NULL) break;
	 if (!strcmp(info.name, palm_dbname[i])) {
	    jpilot_logf(LOG_DEBUG, "Found extra DB\n");
	    found=1;
	    break;
	 }
      }

      if (!found) {
	 continue;
      }

      strncpy(db_copy_name, info.name, MAX_DBNAME-5);
      db_copy_name[MAX_DBNAME-5]='\0';
      if (info.flags & dlpDBFlagResource) {
	 strcat(db_copy_name,".prc");
      } else {
	 strcat(db_copy_name,".pdb");
      }

      filename_make_legal(db_copy_name);

      get_home_file_name(db_copy_name, full_name, 255);

      statb.st_mtime = 0;

      stat(full_name, &statb);

      /* If modification times are the same then we don t need to fetch it */
      if (info.modifyDate == statb.st_mtime) {
	 writef(pipe_out, _("%s is up to date, fetch skipped.\n"), db_copy_name);
	 continue;
      }
      
      writef(pipe_out, _("Fetching '%s'... "), info.name);
      
      info.flags &= 0xff;
      
      pi_fp = pi_file_create(full_name, &info);

      if (pi_fp==0) {
	 writef(pipe_out, "Failed, unable to create file %s\n", full_name);
	 continue;
      }
      if (pi_file_retrieve(pi_fp, sd, 0)<0) {
	 writef(pipe_out, "Failed, unable to back up database\n");
	 times.actime = 0;
	 times.modtime = 0;
      } else {
	 writef(pipe_out, _("OK\n"));
	 times.actime = info.createDate;
	 times.modtime = info.modifyDate;
      }
      pi_file_close(pi_fp);
      
      /*Set the create and modify times of local file to same as on palm */
      utime(full_name, &times);
   }
   return 0;
}

/*
 * Fetch the databases from the palm if modified
 */
int sync_fetch(int sd, unsigned int flags, const int num_backups, int fast_sync)
{
#define MAX_DBNAME 50
   struct pi_file *pi_fp;
   char full_name[256];
   char full_backup_name[300];
   char creator[6];
   struct stat statb;
   struct utimbuf times;
   int i;
   int main_app;
   int mode;
   int manual_skip;
   int cardno, start;
   struct DBInfo info;
   char db_copy_name[MAX_DBNAME];
   char *palm_dbname[]={
      "DatebookDB",
      "AddressDB",
      "ToDoDB",
      "MemoDB",
      "Saved Preferences",
      NULL
   };
   char *extra_dbname[]={
      "Saved Preferences",
      NULL
   };
   char *skip_creators[]={
      /* Take this out if you want to backup AvantGo files */
      "AvGo",
      NULL
   };
#ifdef ENABLE_PLUGINS
   GList *plugin_list, *temp_list;
   struct plugin_s *plugin;
#endif
   
   /*
    * Here are the possibilities for mode:
    * 0. slow sync                  fetch DBs if main app
    * 1. slow sync && full backup   fetch DBs if main app, copy DB to backup
    * 2. fast sync                  return
    * 3. fast sync && full backup   fetch DBs in backup dir
    */
   jpilot_logf(LOG_DEBUG, "sync_fetch flags=0x%x, num_backups=%d, fast=%d\n",
	       flags, num_backups, fast_sync);
   
   mode = ((flags & SYNC_FULL_BACKUP) ? 1:0) + (fast_sync ? 2:0);

   if (mode == 2) {
      fetch_extra_DBs(sd, extra_dbname);
      return 0;
   }
   
   if ((flags & SYNC_FULL_BACKUP)) {
      jpilot_logf(LOG_DEBUG, "Full Backup\n");
      pi_watchdog(sd,10); /* prevent from timing out on long copy times */
      sync_rotate_backups(num_backups);
      pi_watchdog(sd,0);  /* back to normal behavior */
   }
   
   start=cardno=0;
   
   while(dlp_ReadDBList(sd, cardno, dlpOpenRead, start, &info)>0) {
      start=info.index+1;
      creator[0] = (info.creator & 0xFF000000) >> 24;
      creator[1] = (info.creator & 0x00FF0000) >> 16,
      creator[2] = (info.creator & 0x0000FF00) >> 8,
      creator[3] = (info.creator & 0x000000FF);
      creator[4] = '\0';
#ifdef JPILOT_DEBUG
      jpilot_logf(LOG_DEBUG, "dbname = %s\n",info.name);
      jpilot_logf(LOG_DEBUG, "exclude from sync = %d\n",info.miscFlags & dlpDBMiscFlagExcludeFromSync);
      jpilot_logf(LOG_DEBUG, "flag backup = %d\n",info.flags & dlpDBFlagBackup);
      /*writef(pipe_out, "type = %x\n",info.type);*/
      jpilot_logf(LOG_DEBUG, "creator = [%s]\n", creator);
#endif
      if (flags & SYNC_FULL_BACKUP) {
	 /* Look at the skip list */
	 manual_skip=0;
	 for (i=0; skip_creators[i]; i++) {
	    if (!strcmp(creator, skip_creators[i])) {
	       writef(pipe_out, _("Skipping %s\n"), info.name);
	       manual_skip=1;
	       break;
	    }
	 }
	 if (manual_skip) {
	    continue;
	 }
      }

      main_app=0;
      for (i=0; palm_dbname[i]; i++) {
	 if (!strcmp(info.name, palm_dbname[i])) {
	    jpilot_logf(LOG_DEBUG, "Found main app\n");
	    main_app = 1;
	    break;
	 }
      }
#ifdef ENABLE_PLUGINS
      plugin_list = get_plugin_list();

      if (!main_app) {
	 for (temp_list = plugin_list; temp_list; temp_list = temp_list->next) {
	    plugin = (struct plugin_s *)temp_list->data;
	    if (!strcmp(info.name, plugin->db_name)) {
	       jpilot_logf(LOG_DEBUG, "Found plugin\n");
	       main_app = 1;
	       break;
	    }
	 }
      }
#endif
      strncpy(db_copy_name, info.name, MAX_DBNAME-5);
      db_copy_name[MAX_DBNAME-5]='\0';
      if (info.flags & dlpDBFlagResource) {
	 strcat(db_copy_name,".prc");
      } else {
	 strcat(db_copy_name,".pdb");
      }

      filename_make_legal(db_copy_name);

      if (!strcmp(db_copy_name, "Graffiti ShortCuts .prc")) {
	 /* Make a special exception for the graffiti shortcuts.
	  * We want to save it as this to avoid the confusion of
	  * having 2 different versions around */
	 strcpy(db_copy_name, "Graffiti ShortCuts.prc");
      }
      get_home_file_name(db_copy_name, full_name, 255);
      get_home_file_name("backup/", full_backup_name, 255);
      strcat(full_backup_name, db_copy_name);

      if ( (mode==0) && (!main_app) ) {
	 continue;
      }
#ifdef JPILOT_DEBUG
      if (main_app) {
	 jpilot_logf(LOG_DEBUG, "main_app is set\n");
      }
#endif

      statb.st_mtime = 0;

      if (main_app && (mode<2)) {
	 stat(full_name, &statb);
      } else {
	 stat(full_backup_name, &statb);
      }
#ifdef JPILOT_DEBUG
      writef(pipe_out, "palm dbtime= %d, local dbtime = %d\n", info.modifyDate, statb.st_mtime);
      writef(pipe_out, "flags=0x%x\n", info.flags);
      writef(pipe_out, "backup_flag=%d\n", info.flags & dlpDBFlagBackup);
#endif
      /* If modification times are the same then we don t need to fetch it */
      if (info.modifyDate == statb.st_mtime) {
	 writef(pipe_out, _("%s is up to date, fetch skipped.\n"), db_copy_name);
	 continue;
      }
      
      writef(pipe_out, _("Fetching '%s'... "), info.name);
      
      info.flags &= 0xff;
      
      if (main_app && (mode<2)) {
	 pi_fp = pi_file_create(full_name, &info);
      } else {
	 pi_fp = pi_file_create(full_backup_name, &info);
      }
      if (pi_fp==0) {
	 writef(pipe_out, "Failed, unable to create file %s\n",
		main_app ? full_name : full_backup_name);
	 continue;
      }
      if (pi_file_retrieve(pi_fp, sd, 0)<0) {
	 writef(pipe_out, "Failed, unable to back up database\n");
	 times.actime = 0;
	 times.modtime = 0;
      } else {
	 writef(pipe_out, _("OK\n"));
	 times.actime = info.createDate;
	 times.modtime = info.modifyDate;
      }
      pi_file_close(pi_fp);
      
      /*Set the create and modify times of local file to same as on palm */
      if (main_app && (mode<2)) {
	 utime(full_name, &times);
      } else {
	 utime(full_backup_name, &times);
      }

      /* This call preserves the file times */
      if ((main_app) && (mode==1)) {
	 jpilot_copy_file(full_name, full_backup_name);
      }
   }
   return 0;
}

static int sync_install(char *filename, int sd)
{
   struct pi_file *f;
   char *Pc;
   char log_entry[256];
   int r, try_again;
   
   Pc=strrchr(filename, '/');
   if (!Pc) {
      Pc = filename;
   } else {
      Pc++;
   }
      
   writef(pipe_out, _("Installing %s... "), Pc);
   f = pi_file_open(filename);
   if (f==0) {
      writef(pipe_out, "\nUnable to open '%s'!\n", filename);
      return -1;
   }
   r = pi_file_install(f, sd, 0);
   if (r<0) {
      try_again = 0;
      /* Here we make a special exception for graffiti */
      if (!strcmp(f->info.name, "Graffiti ShortCuts")) {
	 strcpy(f->info.name, "Graffiti ShortCuts ");
	 /* This requires a reset */
	 f->info.flags |= dlpDBFlagReset;
	 f->info.flags |= dlpDBFlagNewer;
	 try_again = 1;
      } else if (!strcmp(f->info.name, "Graffiti ShortCuts ")) {
	 strcpy(f->info.name, "Graffiti ShortCuts");
	 /* This requires a reset */
	 f->info.flags |= dlpDBFlagReset;
	 f->info.flags |= dlpDBFlagNewer;
	 try_again = 1;
      }
      if (try_again) {
	 /* Try again */
	 r = pi_file_install(f, sd, 0);
      }
   }

   if (r<0) {
      g_snprintf(log_entry, 255, _("Install %s failed"), Pc);
      log_entry[255]='\0';
      dlp_AddSyncLogEntry(sd, log_entry);
      dlp_AddSyncLogEntry(sd, "\n\r");;
      writef(pipe_out, "Failed.\n");
      pi_file_close(f);
      return -1;
   }
   else {
      /* the space after the %s is a hack, the last char gets cut off */
      g_snprintf(log_entry, 255, _("Installed %s "), Pc);
      log_entry[255]='\0';
      dlp_AddSyncLogEntry(sd, log_entry);
      dlp_AddSyncLogEntry(sd, "\n\r");;
      writef(pipe_out, _("OK\n"));
   }
   pi_file_close(f);

   return 0;
}


/*file must not be open elsewhere when this is called */
/*the first line is 0 */
static int sync_process_install_file(int sd)
{
   FILE *in;
   FILE *out;
   char line[1002];
   char *Pc;
   int r, line_count;

   in = jp_open_home_file("jpilot_to_install", "r");
   if (!in) {
      writef(pipe_out, "Cannot open jpilot_to_install file\n");
      return -1;
   }

   out = jp_open_home_file("jpilot_to_install.tmp", "w");
   if (!out) {
      writef(pipe_out, "Cannot open jpilot_to_install.tmp file\n");
      fclose(in);
      return -1;
   }
   
   for (line_count=0; (!feof(in)); line_count++) {
      line[0]='\0';
      Pc = fgets(line, 1000, in);
      if (!Pc) {
	 break;
      }
      if (line[strlen(line)-1]=='\n') {
	 line[strlen(line)-1]='\0';
      }
      r = sync_install(line, sd);
      if (r==0) {
	 continue;
      }
      fprintf(out, "%s\n", line);
   }
   fclose(in);
   fclose(out);
   
   rename_file("jpilot_to_install.tmp", "jpilot_to_install");
   
   return 0;
}

int is_backup_dir(char *name)
{
   int i;
   
   /* backup dirs are of the form backupMMDDHHMM */
   if (strncmp(name, "backup", 6)) {
      return 0;
   }
   for (i=6; i<14; i++) {
      if (name[i]=='\0') {
	 return 0;
      }
      if (!isdigit(name[i])) {
	 return 0;
      }
   }
   if (name[i]!='\0') {
      return 0;
   }
   return 1;
}

   
static int compare_back_dates(char *s1, char *s2)
{
   /* backupMMDDhhmm */
   int i1, i2;
   
   if ((strlen(s1) < 8) || (strlen(s2) < 8)) {
      return 0;
   }
   i1 = atoi(&s1[6]);
   i2 = atoi(&s2[6]);
   /* Try to guess the year crossover with a 6 month window */
   if (((i1/1000000) <= 3) && ((i2/1000000) >= 10)) {
      return 1;
   }
   if (((i1/1000000) >= 10) && ((i2/1000000) <= 3)) { 
      return 2;
   }
   if (i1>i2) {
      return 1;
   }
   if (i1<i2) {
      return 2;
   }
   return 0;
}

int sync_remove_r(char *full_path)
{
   DIR *dir;
   struct dirent *dirent;
   char full_src[300];
   char last4[8];
   int len;

   dir = opendir(full_path);
   if (dir) {
      while ((dirent = readdir(dir))) {
	 sprintf(full_src, "%s/%s", full_path, dirent->d_name);
	 /* Just to make sure nothing too wrong is deleted */
	 len = strlen(dirent->d_name);
	 if (len < 4) {
	    continue;
	 }
	 strcpy(last4, dirent->d_name+len-4);
	 if ((strcmp(last4, ".pdb")==0) || 
	     (strcmp(last4, ".prc")==0) ||
	     (strcmp(last4, ".pqa")==0)) {
	    unlink(full_src);
	 }
      }
      closedir(dir);
   }
   rmdir(full_path);

   return 0;
}

static int get_oldest_newest_dir(char *oldest, char *newest, int *count)
{
   DIR *dir;
   struct dirent *dirent;
   char home_dir[256];
   int r;

   get_home_file_name("", home_dir, 255);
   jpilot_logf(LOG_DEBUG, "rotate_backups: opening dir %s\n", home_dir);
   *count = 0;
   oldest[0]='\0';
   newest[0]='\0';
   dir = opendir(home_dir);
   if (!dir) {
      return -1;
   }
   *count = 0;
   while((dirent = readdir(dir))) {
      if (is_backup_dir(dirent->d_name)) {
	 jpilot_logf(LOG_DEBUG, "backup dir [%s]\n", dirent->d_name);
	 (*count)++;
	 if (oldest[0]=='\0') {
	    strcpy(oldest, dirent->d_name);
	    /* jpilot_logf(LOG_DEBUG, "oldest is now %s\n", oldest);*/
	 }
	 if (newest[0]=='\0') {
	    strcpy(newest, dirent->d_name);
	    /*jpilot_logf(LOG_DEBUG, "newest is now %s\n", newest);*/
	 }
	 r = compare_back_dates(oldest, dirent->d_name);
	 if (r==1) {
	    strcpy(oldest, dirent->d_name);
	    /*jpilot_logf(LOG_DEBUG, "oldest is now %s\n", oldest);*/
	 }
	 r = compare_back_dates(newest, dirent->d_name);
	 if (r==2) {
	    strcpy(newest, dirent->d_name);
	    /*jpilot_logf(LOG_DEBUG, "newest is now %s\n", newest);*/
	 }
      }
   }
   closedir(dir);
   return 0;
}
      
static int sync_rotate_backups(const int num_backups)
{
   DIR *dir;
   struct dirent *dirent;
   char home_dir[256];
   char full_name[300];
   char full_newdir[300];
   char full_backup[300];
   char full_oldest[300];
   char full_src[300];
   char full_dest[300];
   int r;
   int count, safety;
   char oldest[20];
   char newest[20];
   char newdir[20];
   time_t ltime;
   struct tm *now;

   get_home_file_name("", home_dir, 255);

   /* We use safety because if removing the directory fails then we
    * will get stuck in an endless loop */
   for (safety=100; safety>0; safety--) {
      r = get_oldest_newest_dir(oldest, newest, &count);
      if (r<0) {
	 jpilot_logf(LOG_WARN, "unable to read home dir\n");
	 break;
      }
      if (count > num_backups) {
	 sprintf(full_oldest, "%s/%s", home_dir, oldest);
	 jpilot_logf(LOG_DEBUG, "count=%d, num_backups=%d\n", count, num_backups);
	 jpilot_logf(LOG_DEBUG, "removing dir [%s]\n", full_oldest);
	 sync_remove_r(full_oldest);
      } else {
	 break;
      }
   }

   /* Now we should have the same number of backups (or less) as num_backups */
   
   time(&ltime);
   now = localtime(&ltime);
   /* Create the new backup directory */
   sprintf(newdir, "backup%02d%02d%02d%02d",
	   now->tm_mon+1, now->tm_mday, now->tm_hour, now->tm_min);
   if (strcmp(newdir, newest)) {
      sprintf(full_newdir, "%s/%s", home_dir, newdir);
      if (mkdir(full_newdir, 0700)==0) {
	 count++;
      }
   }

   /* Copy from the newest backup, if it exists */
   if (strcmp(newdir, newest)) {
      sprintf(full_backup, "%s/backup", home_dir);
      sprintf(full_newdir, "%s/%s", home_dir, newdir);
      dir = opendir(full_backup);
      if (dir) {
	 while ((dirent = readdir(dir))) {
	    sprintf(full_src, "%s/%s", full_backup, dirent->d_name);
	    sprintf(full_dest, "%s/%s", full_newdir, dirent->d_name);
	    jpilot_copy_file(full_src, full_dest);
	 }
	 closedir(dir);
      }
   }

   /* Remove the oldest backup if needed */
   if (count > num_backups) {
      if ( (oldest[0]!='\0') && (strcmp(newdir, oldest)) ) {
	 sprintf(full_oldest, "%s/%s", home_dir, oldest);
	 jpilot_logf(LOG_DEBUG, "removing dir [%s]\n", full_oldest);
	 sync_remove_r(full_oldest);
      }
   }

   /* Delete the symlink */
   sprintf(full_name, "%s/backup", home_dir);
   unlink(full_name);
   
   /* Create the symlink */
   symlink(newdir, full_name);
   
   return 0;
}

void recode_packed_record(char *DB_name, void *record, int rec_len, long char_set)
{
   /*todo move this to before the record is written out?*/
   /* Convert to SJIS Japanese Kanji code (Palm use this code) */
   /* or convert to different encoding */
   /*Write the record to the Palm Pilot */
   if (!strcmp(DB_name, "DatebookDB")) {
      struct Appointment a;
      unpack_Appointment(&a, record, rec_len);
      if (char_set == CHAR_SET_JAPANESE) Euc2Sjis(a.description, 65536);
      if (char_set == CHAR_SET_1250) Lat2Win(a.description, 65536);
      if (char_set == CHAR_SET_1251) koi8_to_win1251(a.description, 65536);
      if (char_set == CHAR_SET_1251_B) win1251_to_koi8(a.description, 65536);
      if (char_set == CHAR_SET_JAPANESE) Euc2Sjis(a.note, 65536);
      if (char_set == CHAR_SET_1250) Lat2Win(a.note, 65536);
      if (char_set == CHAR_SET_1251) koi8_to_win1251(a.note, 65536);
      if (char_set == CHAR_SET_1251_B) win1251_to_koi8(a.note, 65536);
      rec_len = pack_Appointment(&a, record, 65535);
   }
   if (!strcmp(DB_name, "AddressDB")) {
      struct Address a;
      int i;
      unpack_Address(&a, record, rec_len);
      for (i = 0; i < 19; i++) {
	 if (char_set == CHAR_SET_JAPANESE) Euc2Sjis(a.entry[i], 65536);
	 if (char_set == CHAR_SET_1250) Lat2Win(a.entry[i], 65536);
	 if (char_set == CHAR_SET_1251) koi8_to_win1251(a.entry[i], 65536);
	 if (char_set == CHAR_SET_1251_B) win1251_to_koi8(a.entry[i], 65536);
      }
      rec_len = pack_Address(&a, record, 65535);
   }
   if (!strcmp(DB_name, "ToDoDB")) {
      struct ToDo t;
      unpack_ToDo(&t, record, rec_len);
      if (char_set == CHAR_SET_JAPANESE) Euc2Sjis(t.description, 65536);
      if (char_set == CHAR_SET_1250) Lat2Win(t.description, 65536);
      if (char_set == CHAR_SET_1251) koi8_to_win1251(t.description, 65536);
      if (char_set == CHAR_SET_1251_B) win1251_to_koi8(t.description, 65536);
      if (char_set == CHAR_SET_JAPANESE) Euc2Sjis(t.note, 65536);
      if (char_set == CHAR_SET_1250) Lat2Win(t.note, 65536);
      if (char_set == CHAR_SET_1251) koi8_to_win1251(t.note, 65536);
      if (char_set == CHAR_SET_1251_B) win1251_to_koi8(t.note, 65536);
      rec_len = pack_ToDo(&t, record, 65535);
   }
   if (!strcmp(DB_name, "MemoDB")) {
      struct Memo m;
      unpack_Memo(&m, record, rec_len);
      if (char_set == CHAR_SET_JAPANESE) Euc2Sjis(m.text, 65536);
      if (char_set == CHAR_SET_1250) Lat2Win(m.text, 65536);
      if (char_set == CHAR_SET_1251) koi8_to_win1251(m.text, 65536);
      if (char_set == CHAR_SET_1251_B) win1251_to_koi8(m.text, 65536);
      rec_len = pack_Memo(&m, record, 65535);
   }
}

int fast_sync_local_recs(char *DB_name, int sd, int db)
{
   unsigned long new_id;
   int ret;
   int num;
   FILE *pc_in;
   PC3RecordHeader header;
   char *record;
   void *Pbuf;
   char pronoun[10];
   int rec_len;
   char pc_filename[256];
   char write_log_message[256];
   char error_log_message_w[256];
   char error_log_message_d[256];
   char delete_log_message[256];
   int index, size, attr, category;
   long char_set;

   jpilot_logf(LOG_DEBUG, "fast_sync_local_recs\n");

   if ((DB_name==NULL) || (strlen(DB_name) > 250)) {
      return -1;
   }
   g_snprintf(pc_filename, 255, "%s.pc3", DB_name);
   /* This is an attempt to use the proper pronoun most of the time */
   if (strchr("aeiou", tolower(DB_name[0]))) {
      strcpy(pronoun, "an");
   } else {
      strcpy(pronoun, "a");
   }
   g_snprintf(write_log_message, 255,
	      "Wrote %s %s record.\n\r", pronoun, DB_name);
   g_snprintf(error_log_message_w, 255,
	      "Writing %s %s record failed.\n\r", pronoun, DB_name);
   g_snprintf(error_log_message_d, 255,
	      "Deleting %s %s record failed.\n\r", pronoun, DB_name);
   g_snprintf(delete_log_message, 256,
	      "Deleted %s %s record.\n\r", pronoun, DB_name);

   pc_in = jp_open_home_file(pc_filename, "r+");
   if (pc_in==NULL) {
      writef(pipe_out, "Unable to open %s\n",pc_filename);
      return -1;
   }

   while(!feof(pc_in)) {
      num = read_header(pc_in, &header);
      if (num!=1) {
	 if (ferror(pc_in)) {
	    break;
	 }
	 if (feof(pc_in)) {
	    break;
	 }
      }
      rec_len = header.rec_len;
      if (rec_len > 0x10000) {
	 writef(pipe_out, "PC file corrupt?\n");
	 fclose(pc_in);
	 return -1;
      }
      /* Case 5: */
      if (header.rt==NEW_PC_REC) {
	 jpilot_logf(LOG_DEBUG, "new pc record\n");
	 record = malloc(rec_len);
	 if (!record) {
	    writef(pipe_out, "fast_sync_local_recs(): Out of memory\n");
	    break;
	 }
	 num = fread(record, rec_len, 1, pc_in);
	 if (num != 1) {
	    if (ferror(pc_in)) {
	       break;
	    }
	 }
	 get_pref(PREF_CHAR_SET, &char_set, NULL);
	 if (char_set==CHAR_SET_JAPANESE ||
	     char_set==CHAR_SET_1250 ||
	     char_set==CHAR_SET_1251 ||
	     char_set==CHAR_SET_1251_B
	     ) {
	    recode_packed_record(DB_name, record, rec_len, char_set);
	 }

	 jpilot_logf(LOG_DEBUG, "Writing PC record to palm\n");

	 ret = dlp_WriteRecord(sd, db, header.attrib & dlpRecAttrSecret,
			       0, header.attrib & 0x0F,
			       record, rec_len, &new_id);
	 
	 jpilot_logf(LOG_DEBUG, "Writing PC record to local\n");
	 if (ret >=0) {
	    pdb_file_modify_record(DB_name, record, rec_len,
				   header.attrib & dlpRecAttrSecret,
				   header.attrib & 0x0F, new_id);
	 }

	 if (record) {
	    free(record);
	    record = NULL;
	 }

	 if (ret < 0) {
	    writef(pipe_out, "dlp_WriteRecord failed\n");
	    dlp_AddSyncLogEntry(sd, error_log_message_w);
	 } else {
	    dlp_AddSyncLogEntry(sd, write_log_message);
	    /* Now mark the record as deleted in the pc file */
	    if (fseek(pc_in, -(header.header_len+rec_len), SEEK_CUR)) {
	       writef(pipe_out, "fseek failed - fatal error\n");
	       fclose(pc_in);
	       return -1;
	    }
	    header.rt=DELETED_PC_REC;
	    write_header(pc_in, &header);
	 }
      }
      /* Case 3: */
      if ((header.rt==DELETED_PALM_REC) || (header.rt==MODIFIED_PALM_REC)) {
	 jpilot_logf(LOG_DEBUG, "deleted or modified pc record\n");
	 rec_len = header.rec_len;
	 record = malloc(rec_len);
	 num = fread(record, rec_len, 1, pc_in);
	 if (num != 1) {
	    if (ferror(pc_in)) {
	       break;
	    }
	 }
	 if (fseek(pc_in, -rec_len, SEEK_CUR)) {
	    writef(pipe_out, "fseek failed - fatal error\n");
	    fclose(pc_in);
	    return -1;
	 }
	 ret = pdb_file_read_record_by_id(DB_name, 
					  header.unique_id,
					  &Pbuf, &size, &index,
					  &attr, &category);
	 /* ret = dlp_ReadRecordById(sd, db, header.unique_id, buffer,
				  &index, &size, &attr, &category); */
#ifdef JPILOT_DEBUG
	 if (ret>=0 ) {
	    printf("read record by id %s returned %d\n", DB_name, ret);
	    printf("id %d, index %d, size %d, attr 0x%x, category %d\n",
		   header.unique_id, index, size, attr, category);
	 }
#endif
	 if ((rec_len == size) && (header.unique_id != 0)) {
	    jpilot_logf(LOG_DEBUG, "sizes match!\n");
	    /* if (memcmp(record, Pbuf, size)==0)
	    jpilot_logf(LOG_DEBUG,"Binary is the same!\n"); */
	    /* writef(pipe_out, "Deleting Palm id=%d,\n",header.unique_id);*/
	    ret = dlp_DeleteRecord(sd, db, 0, header.unique_id);
	    if (ret < 0) {
	       writef(pipe_out, "dlp_DeleteRecord failed\n"
		      "This could be because the record was already deleted on the Palm\n");
	       dlp_AddSyncLogEntry(sd, error_log_message_d);
	    } else {
	       dlp_AddSyncLogEntry(sd, delete_log_message);
	       pdb_file_delete_record_by_id(DB_name, header.unique_id);
	    }
	 }
	 
	 /*Now mark the record as deleted */
	 if (fseek(pc_in, -header.header_len, SEEK_CUR)) {
	    writef(pipe_out, "fseek failed - fatal error\n");
	    fclose(pc_in);
	    return -1;
	 }
	 header.rt=DELETED_DELETED_PALM_REC;
	 write_header(pc_in, &header);
      }

      /*skip this record now that we are done with it */
      if (fseek(pc_in, rec_len, SEEK_CUR)) {
	 writef(pipe_out, "fseek failed - fatal error\n");
	 fclose(pc_in);
	 return -1;
      }
   }
   fclose(pc_in);

   return 0;
}

int pdb_file_delete_record_by_id(char *DB_name, pi_uid_t uid_in)
{
   char local_pdb_file[256];
   char full_local_pdb_file[256];
   char full_local_pdb_file2[256];
   struct pi_file *pf1, *pf2;
   struct DBInfo infop;
   void *app_info;
   void *sort_info;
   void *record;
   int r;
   int idx;
   int size;
   int attr;
   int cat;
   pi_uid_t uid;

   jpilot_logf(LOG_DEBUG, "pdb_file_delete_record_by_id\n");

   g_snprintf(local_pdb_file, 250, "%s.pdb", DB_name);
   get_home_file_name(local_pdb_file, full_local_pdb_file, 250);
   strcpy(full_local_pdb_file2, full_local_pdb_file);
   strcat(full_local_pdb_file2, "2");

   pf1 = pi_file_open(full_local_pdb_file);
   if (pf1<=0) {
      jpilot_logf(LOG_WARN, "Couldn't open [%s]\n", full_local_pdb_file);
      return -1;
   }
   pi_file_get_info(pf1, &infop);
   pf2 = pi_file_create(full_local_pdb_file2, &infop);
   if (pf2<=0) {
      jpilot_logf(LOG_WARN, "Couldn't open [%s]\n", full_local_pdb_file2);
      return -1;
   }

   pi_file_get_app_info(pf1, &app_info, &size);
   pi_file_set_app_info(pf2, app_info, size);

   pi_file_get_sort_info(pf1, &sort_info, &size);  
   pi_file_set_sort_info(pf2, sort_info, size);

   for(idx=0;;idx++) {
      r = pi_file_read_record(pf1, idx, &record, &size, &attr, &cat, &uid);
      if (r<0) break;
      if (uid==uid_in) continue;
      pi_file_append_record(pf2, record, size, attr, cat, uid);
   }

   pi_file_close(pf1);
   pi_file_close(pf2);
   
   if (rename(full_local_pdb_file2, full_local_pdb_file) < 0) {
      jpilot_logf(LOG_WARN, "delete: rename failed\n");
   }

   return 0;
}

/*
 * Original ID is in the case of a modification
 * new ID is used in the case of an add record
 */
int pdb_file_modify_record(char *DB_name, void *record_in, int size_in,
			   int attr_in, int cat_in, pi_uid_t uid_in)
{
   char local_pdb_file[256];
   char full_local_pdb_file[256];
   char full_local_pdb_file2[256];
   struct pi_file *pf1, *pf2;
   struct DBInfo infop;
   void *app_info;
   void *sort_info;
   void *record;
   int r;
   int idx;
   int size;
   int attr;
   int cat;
   int found;
   pi_uid_t uid;

   jpilot_logf(LOG_DEBUG, "pi_file_modify_record\n");

   g_snprintf(local_pdb_file, 250, "%s.pdb", DB_name);
   get_home_file_name(local_pdb_file, full_local_pdb_file, 250);
   strcpy(full_local_pdb_file2, full_local_pdb_file);
   strcat(full_local_pdb_file2, "2");

   pf1 = pi_file_open(full_local_pdb_file);
   if (pf1<=0) {
      jpilot_logf(LOG_WARN, "Couldn't open [%s]\n", full_local_pdb_file);
      return -1;
   }
   pi_file_get_info(pf1, &infop);
   pf2 = pi_file_create(full_local_pdb_file2, &infop);
   if (pf2<=0) {
      jpilot_logf(LOG_WARN, "Couldn't open [%s]\n", full_local_pdb_file2);
      return -1;
   }

   pi_file_get_app_info(pf1, &app_info, &size);
   pi_file_set_app_info(pf2, app_info, size);

   pi_file_get_sort_info(pf1, &sort_info, &size);  
   pi_file_set_sort_info(pf2, sort_info, size);

   found = 0;
   
   for(idx=0;;idx++) {
      r = pi_file_read_record(pf1, idx, &record, &size, &attr, &cat, &uid);
      if (r<0) break;
      if (uid==uid_in) {
	 pi_file_append_record(pf2, record_in, size_in, attr_in, cat_in, uid_in);
	 found=1;
      } else {
	 pi_file_append_record(pf2, record, size, attr, cat, uid);
      }
   }
   if (!found) {
      pi_file_append_record(pf2, record_in, size_in, attr_in, cat_in, uid_in);
   }
   
   pi_file_close(pf1);
   pi_file_close(pf2);
   
   if (rename(full_local_pdb_file2, full_local_pdb_file) < 0) {
      jpilot_logf(LOG_WARN, "modify: rename failed\n");
   }
   
   return 0;
}

int pdb_file_read_record_by_id(char *DB_name, 
			       pi_uid_t uid,
			       void **bufp, int *sizep, int *idxp,
			       int *attrp, int * catp)
{
   char local_pdb_file[256];
   char full_local_pdb_file[256];
   struct pi_file *pf1;
   int r;

   jpilot_logf(LOG_DEBUG, "pdb_file_read_record_by_id\n");
   
   g_snprintf(local_pdb_file, 250, "%s.pdb", DB_name);
   get_home_file_name(local_pdb_file, full_local_pdb_file, 250);

   pf1 = pi_file_open(full_local_pdb_file);
   if (pf1<=0) {
      jpilot_logf(LOG_WARN, "Couldn't open [%s]\n", full_local_pdb_file);
      return -1;
   }
   
   r = pi_file_read_record_by_id(pf1, uid, bufp, sizep, idxp, attrp, catp);
   
   pi_file_close(pf1);
   
   return r;
}

int pdb_file_count_recs(char *DB_name, int *num)
{
   char local_pdb_file[256];
   char full_local_pdb_file[256];
   struct pi_file *pf1;

   jpilot_logf(LOG_DEBUG, "pdb_file_count_recs\n");

   *num = 0;

   g_snprintf(local_pdb_file, 250, "%s.pdb", DB_name);
   get_home_file_name(local_pdb_file, full_local_pdb_file, 250);

   pf1 = pi_file_open(full_local_pdb_file);
   if (pf1<=0) {
      jpilot_logf(LOG_WARN, "Couldn't open [%s]\n", full_local_pdb_file);
      return -1;
   }
   
   *num = pf1->nentries;
   
   pi_file_close(pf1);
   
   return 0;
}

int pdb_file_write_app_block(char *DB_name, void *bufp, int size_in)
{
   char local_pdb_file[256];
   char full_local_pdb_file[256];
   char full_local_pdb_file2[256];
   struct pi_file *pf1, *pf2;
   struct DBInfo infop;
   void *app_info;
   void *sort_info;
   void *record;
   int r;
   int idx;
   int size;
   int attr;
   int cat;
   pi_uid_t uid;

   jpilot_logf(LOG_DEBUG, "pi_file_write_app_block\n");

   g_snprintf(local_pdb_file, 250, "%s.pdb", DB_name);
   get_home_file_name(local_pdb_file, full_local_pdb_file, 250);
   strcpy(full_local_pdb_file2, full_local_pdb_file);
   strcat(full_local_pdb_file2, "2");

   pf1 = pi_file_open(full_local_pdb_file);
   if (pf1<=0) {
      jpilot_logf(LOG_WARN, "Couldn't open [%s]\n", full_local_pdb_file);
      return -1;
   }
   pi_file_get_info(pf1, &infop);
   pf2 = pi_file_create(full_local_pdb_file2, &infop);
   if (pf2<=0) {
      jpilot_logf(LOG_WARN, "Couldn't open [%s]\n", full_local_pdb_file2);
      return -1;
   }

   pi_file_get_app_info(pf1, &app_info, &size);
   pi_file_set_app_info(pf2, bufp, size_in);

   pi_file_get_sort_info(pf1, &sort_info, &size);  
   pi_file_set_sort_info(pf2, sort_info, size);

   for(idx=0;;idx++) {
      r = pi_file_read_record(pf1, idx, &record, &size, &attr, &cat, &uid);
      if (r<0) break;
      pi_file_append_record(pf2, record, size, attr, cat, uid);
   }
   
   pi_file_close(pf1);
   pi_file_close(pf2);
   
   if (rename(full_local_pdb_file2, full_local_pdb_file) < 0) {
      jpilot_logf(LOG_WARN, "write_app_block: rename failed\n");
   }
   
   return 0;
}
/*
 * This code does not do archiving.
 *
 * For each remote record (RR):
 *   Case 1:
 *   if RR deleted or archived
 *     remove local record (LR)
 *   Case 2:
 *   if RR changed
 *     change LR, If it doesn't exist then add it
 * For each LR
 *   Case 3:
 *   if LR deleted or archived
 *     if RR==OLR (Original LR) remove RR, and LR
 *   Case 4:
 *   if LR changed
 *     We have a new local record (NLR) and a 
 *        modified (deleted) local record (MLR)
 *     if NLR==RR then do nothing (either both were changed equally, or
 *				   local was changed and changed back)
 *     add NLR to remote, if RR==LR remove RR
 *   Case 5:
 *   if new LR
 *     add LR to remote
 */
int fast_sync_application(char *DB_name, int sd)
{
   int db;
   int ret;
   char pronoun[10];
   char write_log_message[256];
   char error_log_message_w[256];
   char error_log_message_d[256];
   char delete_log_message[256];
   char log_entry[256];
   recordid_t id=0;
   int index, size, attr, category;
   int local_num, palm_num;
   unsigned char buffer[65536];
   char *extra_dbname[2];

   jpilot_logf(LOG_DEBUG, "fast_sync_application %s\n", DB_name);
   
   if ((DB_name==NULL) || (strlen(DB_name) == 0) || (strlen(DB_name) > 250)) {
      return -1;
   }
   g_snprintf(log_entry, 255, "Syncing %s\n", DB_name);
   log_entry[255]='\0';
   writef(pipe_out, log_entry);

   /* This is an attempt to use the proper pronoun most of the time */
   if (strchr("aeiou", tolower(DB_name[0]))) {
      strcpy(pronoun, "an");
   } else {
      strcpy(pronoun, "a");
   }
   g_snprintf(write_log_message, 255,
	      "Wrote %s %s record.\n\r", pronoun, DB_name);
   g_snprintf(error_log_message_w, 255,
	      "Writing %s %s record failed.\n\r", pronoun, DB_name);
   g_snprintf(error_log_message_d, 255,
	      "Deleting %s %s record failed.\n\r", pronoun, DB_name);
   g_snprintf(delete_log_message, 256,
	      "Deleted %s %s record.\n\r", pronoun, DB_name);

   /* Open the applications database, store access handle in db */
   if (dlp_OpenDB(sd, 0, dlpOpenReadWrite|dlpOpenSecret, DB_name, &db) < 0) {
      g_snprintf(log_entry, 255, "Unable to open %s\n\r", DB_name);
      log_entry[255]='\0';
      dlp_AddSyncLogEntry(sd, log_entry);
      return -1;
   }

   /* I can't get the appinfodirty flag to work, so I do this for now */
   ret = dlp_ReadAppBlock(sd, db, 0, buffer, 65535);
   jpilot_logf(LOG_DEBUG, "readappblock ret=%d\n", ret);
   if (ret>0) {
      pdb_file_write_app_block(DB_name, buffer, ret);
   }

   while(1) {
      ret = dlp_ReadNextModifiedRec(sd, db, buffer,
				    &id, &index, &size, &attr, &category);
      if (ret>=0 ) {
	 jpilot_logf(LOG_DEBUG, "read next record for %s returned %d\n", DB_name, ret);
	 jpilot_logf(LOG_DEBUG, "id %ld, index %d, size %d, attr 0x%x, category %d\n",id, index, size, attr, category);
      } else {
	 break;
      }
      /* Case 1: */
      if ((attr &  dlpRecAttrDeleted) || (attr & dlpRecAttrArchived)) {
	 jpilot_logf(LOG_DEBUG, "found a deleted record on palm\n");
	 pdb_file_delete_record_by_id(DB_name, id);
	 continue;
      }
      /* Case 2: */
      /* Note that if deleted we don't want to deal with it (taken care of above) */
      if (attr & dlpRecAttrDirty) {
	 jpilot_logf(LOG_DEBUG, "found a deleted record on palm\n");
	 pdb_file_modify_record(DB_name, buffer, size, attr, category, id);
      }
   }

   fast_sync_local_recs(DB_name, sd, db);

   dlp_ResetSyncFlags(sd, db);
   dlp_CleanUpDatabase(sd, db);

   /* Count the number of records, should be equal, may not be */
   dlp_ReadOpenDBInfo(sd, db, &palm_num);
   pdb_file_count_recs(DB_name, &local_num);
#ifdef JPILOT_DEBUG
   writef(pipe_out ,"palm: number of records = %d\n", palm_num);
   writef(pipe_out ,"disk: number of records = %d\n", local_num);
#endif

   dlp_CloseDB(sd, db);

   if (local_num != palm_num) {
      extra_dbname[0] = DB_name;
      extra_dbname[1] = NULL;
      fetch_extra_DBs(sd, extra_dbname);
   }

   return 0;
}
