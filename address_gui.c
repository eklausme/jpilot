/* address_gui.c
 * A module of J-Pilot http://jpilot.org
 * 
 * Copyright (C) 1999-2002 by Judd Montgomery
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
#include <sys/stat.h>
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <gdk/gdk.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "utils.h"
#include "address.h"
#include "log.h"
#include "prefs.h"
#include "print.h"
#include "password.h"
#include "export.h"
#include <pi-dlp.h>
/* There are a large number of calls to gtk_text_insert in the code.  To
 * add ifdef/endif blocks around all of them would make the code unreadable.
 * Instead, I use a macro substitution to convert old GTK 1.X calls to 
 * GTK 2.X calls. */
#ifdef ENABLE_GTK2
#define GTK_TEXT(arg1) GTK_TEXT_BUFFER(gtk_txt_buf_ ## arg1)
#define gtk_text_insert(buffer,arg2,arg3,arg4,string,length) gtk_text_buffer_insert_at_cursor(buffer,string,length)
#endif


/*#define SHADOW GTK_SHADOW_IN */
/*#define SHADOW GTK_SHADOW_OUT */
/*#define SHADOW GTK_SHADOW_ETCHED_IN */
#define SHADOW GTK_SHADOW_ETCHED_OUT

#define CONNECT_SIGNALS 400
#define DISCONNECT_SIGNALS 401

#define NUM_MENU_ITEM1 8
#define NUM_MENU_ITEM2 8
#define NUM_ADDRESS_CAT_ITEMS 16
#define NUM_ADDRESS_ENTRIES 19
#define NUM_ADDRESS_LABELS 22
#define NUM_PHONE_ENTRIES 5

/* There are 3 extra fields for Japanese Palm OS's KANA extension in address book.
 * Kana means 'pronounce of name'.
 */
#define NUM_ADDRESS_EXT_ENTRIES 3

int order[NUM_ADDRESS_ENTRIES]={
   0,1,13,2,3,4,5,6,7,8,9,10,11,12,14,15,16,17,18 };
int order_ja[NUM_ADDRESS_ENTRIES + NUM_ADDRESS_EXT_ENTRIES] = {
   19,20,0,1,21,13,2,3,4,5,6,7,8,9,10,11,12,14,15,16,17,18 };
char *field_names[]={"Last", "First", "Title", "Company", "Phone1",
     "Phone2", "Phone3", "Phone4", "Phone5", "Address", "City", "State",
     "ZipCode", "Country", "Custom1", "Custom2", "Custom3", "Custom4",
     "Note", "phoneLabel1", "phoneLabel2", "phoneLabel3", "phoneLabel4",
     "phoneLabel5", "showPhone", NULL};
char *field_names_ja[]={"kana(Last)", "Last",  "kana(First)", "First",
     "Title", "kana(Company)","Company", "Phone1",
     "Phone2", "Phone3", "Phone4", "Phone5", "Address", "City", "State",
     "ZipCode", "Country", "Custom1", "Custom2", "Custom3", "Custom4",
     "Note", "phoneLabel1", "phoneLabel2", "phoneLabel3", "phoneLabel4",
     "phoneLabel5", "showPhone", NULL};

#define ADDRESS_NAME_COLUMN  0
#define ADDRESS_NOTE_COLUMN  1
#define ADDRESS_PHONE_COLUMN 2

#define ADDRESS_MAX_CLIST_NAME 30
#define ADDRESS_MAX_COLUMN_LEN 80

GtkWidget *clist;
GtkWidget *address_text[NUM_ADDRESS_ENTRIES+NUM_ADDRESS_EXT_ENTRIES];
#ifdef ENABLE_GTK2
static GObject *gtk_txt_buf_address_text[NUM_ADDRESS_ENTRIES+NUM_ADDRESS_EXT_ENTRIES];
#endif
GtkWidget *text;
#ifdef ENABLE_GTK2
static GObject *gtk_txt_buf_text;
#endif
#ifndef ENABLE_GTK2
GtkWidget *vscrollbar;
#endif
static GtkWidget *private_checkbox;
GtkWidget *phone_list_menu[NUM_PHONE_ENTRIES];
GtkWidget *menu;
GtkWidget *menu_item[NUM_MENU_ITEM1][NUM_MENU_ITEM2];
/*We need an extra one for the ALL category */
GtkWidget *address_cat_menu_item1[NUM_ADDRESS_CAT_ITEMS+1];
GtkWidget *address_cat_menu_item2[NUM_ADDRESS_CAT_ITEMS];
static GtkWidget *category_menu1;
static GtkWidget *category_menu2;
GtkWidget *address_quickfind_entry;
static GtkWidget *notebook;
static GtkWidget *pane;
static GtkWidget *radio_button[NUM_PHONE_ENTRIES];
static GtkWidget *dial_button[NUM_PHONE_ENTRIES];

static struct AddressAppInfo address_app_info;
static struct sorted_cats sort_l[NUM_ADDRESS_CAT_ITEMS];
int address_category=CATEGORY_ALL;
int address_phone_label_selected[NUM_PHONE_ENTRIES];
static int clist_row_selected;
extern GtkTooltips *glob_tooltips;

static AddressList *glob_address_list=NULL;
static AddressList *export_address_list=NULL;

static GtkWidget *new_record_button;
static GtkWidget *apply_record_button;
static GtkWidget *add_record_button;
static GtkWidget *delete_record_button;
static GtkWidget *undelete_record_button;
static GtkWidget *copy_record_button;
static int record_changed;
static int clist_hack;

static void connect_changed_signals(int con_or_dis);
static void address_update_clist(GtkWidget *clist, GtkWidget *tooltip_widget,
				 AddressList **addr_list, int category, int main);
int address_clist_redraw();
static int address_find();
static void get_address_attrib(unsigned char *attrib);
static void cb_clist_selection(GtkWidget      *clist,
			       gint           row,
			       gint           column,
			       GdkEventButton *event,
			       gpointer       data);

static void init()
{
   int i, j;
   record_changed=CLEAR_FLAG;
   for (i=0; i<NUM_MENU_ITEM1; i++) {
      for (j=0; j<NUM_MENU_ITEM2; j++) {
	 menu_item[i][j] = NULL;
      }
   }
   for (i=0; i<NUM_ADDRESS_CAT_ITEMS; i++) {
      address_cat_menu_item2[i] = NULL;
   }
}

static void
set_new_button_to(int new_state)
{
   jp_logf(JP_LOG_DEBUG, "set_new_button_to new %d old %d\n", new_state, record_changed);

   if (record_changed==new_state) {
      return;
   }

   switch (new_state) {
    case MODIFY_FLAG:
      gtk_clist_set_selection_mode(GTK_CLIST(clist), GTK_SELECTION_SINGLE);
      clist_hack=TRUE;
      /* The line selected on the clist becomes unhighlighted, so we do this */
      gtk_clist_select_row(GTK_CLIST(clist), clist_row_selected, 0);
      gtk_widget_show(apply_record_button);
      gtk_widget_hide(delete_record_button);
      break;
    case NEW_FLAG:
      gtk_clist_set_selection_mode(GTK_CLIST(clist), GTK_SELECTION_SINGLE);
      clist_hack=TRUE;
      /* The line selected on the clist becomes unhighlighted, so we do this */
      gtk_clist_select_row(GTK_CLIST(clist), clist_row_selected, 0);
      gtk_widget_show(add_record_button);
      gtk_widget_hide(copy_record_button);
      gtk_widget_hide(delete_record_button);
      break;
    case CLEAR_FLAG:
      gtk_clist_set_selection_mode(GTK_CLIST(clist), GTK_SELECTION_BROWSE);
      clist_hack=FALSE;
      gtk_widget_show(new_record_button);
      gtk_widget_show(copy_record_button);
      gtk_widget_show(delete_record_button);
      break;
    case UNDELETE_FLAG:
      gtk_clist_set_selection_mode(GTK_CLIST(clist), GTK_SELECTION_BROWSE);
      clist_hack=FALSE;
      gtk_widget_hide(delete_record_button);
      gtk_widget_show(undelete_record_button);
      break;
    default:
      return;
   }
   switch (record_changed) {
    case MODIFY_FLAG:
      gtk_widget_hide(apply_record_button);
      gtk_widget_show(copy_record_button);
      gtk_widget_show(delete_record_button);
      break;
    case NEW_FLAG:
      gtk_widget_hide(add_record_button);
      gtk_widget_show(copy_record_button);
      gtk_widget_show(delete_record_button);
      break;
    case CLEAR_FLAG:
      if (new_state != UNDELETE_FLAG)
      {
         gtk_widget_hide(new_record_button);
         gtk_widget_hide(delete_record_button);
      }
      break;
    case UNDELETE_FLAG:
      gtk_widget_hide(undelete_record_button);
      gtk_widget_show(delete_record_button);
      break;
   }
   record_changed=new_state;
}

static void
cb_record_changed(GtkWidget *widget,
		  gpointer   data)
{
   jp_logf(JP_LOG_DEBUG, "cb_record_changed\n");
   if (record_changed==CLEAR_FLAG) {
      connect_changed_signals(DISCONNECT_SIGNALS);
      if (((GtkCList *)clist)->rows > 0) {
	 set_new_button_to(MODIFY_FLAG);
      } else {
	 set_new_button_to(NEW_FLAG);
      }
   }
   return;
}

static void connect_changed_signals(int con_or_dis)
{
   int i, j;
   static int connected=0;
   long use_jos, char_set;

   get_pref(PREF_CHAR_SET, &char_set, NULL);
   get_pref(PREF_USE_JOS, &use_jos, NULL);

   /* CONNECT */
   if ((con_or_dis==CONNECT_SIGNALS) && (!connected)) {
      connected=1;

      for (i=0; i<NUM_MENU_ITEM1; i++) {
	 for (j=0; j<NUM_MENU_ITEM2; j++) {
	    if (menu_item[i][j]) {
	       gtk_signal_connect(GTK_OBJECT(menu_item[i][j]), "toggled",
				  GTK_SIGNAL_FUNC(cb_record_changed), NULL);
	    }
	 }
      }
      for (i=0; i<NUM_PHONE_ENTRIES; i++) {
	 if (radio_button[i]) {
	    gtk_signal_connect(GTK_OBJECT(radio_button[i]), "toggled",
			       GTK_SIGNAL_FUNC(cb_record_changed), NULL);
	 }
      }
      for (i=0; i<NUM_ADDRESS_CAT_ITEMS; i++) {
	 if (address_cat_menu_item2[i]) {
	    gtk_signal_connect(GTK_OBJECT(address_cat_menu_item2[i]), "toggled",
			       GTK_SIGNAL_FUNC(cb_record_changed), NULL);
	 }
      }
      if (!use_jos && (char_set == CHAR_SET_JAPANESE)) {
	 for (i=0; i<(NUM_ADDRESS_ENTRIES+NUM_ADDRESS_EXT_ENTRIES); i++) {
#ifdef ENABLE_GTK2
	    g_signal_connect(gtk_txt_buf_address_text[i], "changed",
			     GTK_SIGNAL_FUNC(cb_record_changed), NULL);
#else
	    gtk_signal_connect(GTK_OBJECT(address_text[i]), "changed",
			       GTK_SIGNAL_FUNC(cb_record_changed), NULL);
#endif
	 }
      } else {
	 for (i=0; i<NUM_ADDRESS_ENTRIES; i++) {
#ifdef ENABLE_GTK2
	    g_signal_connect(gtk_txt_buf_address_text[i], "changed",
			     GTK_SIGNAL_FUNC(cb_record_changed), NULL);
#else
	    gtk_signal_connect(GTK_OBJECT(address_text[i]), "changed",
			       GTK_SIGNAL_FUNC(cb_record_changed), NULL);
#endif
	 }
      }
      gtk_signal_connect(GTK_OBJECT(private_checkbox), "toggled",
			 GTK_SIGNAL_FUNC(cb_record_changed), NULL);
   }

   /* DISCONNECT */
   if ((con_or_dis==DISCONNECT_SIGNALS) && (connected)) {
      connected=0;
      for (i=0; i<NUM_MENU_ITEM1; i++) {
	 for (j=0; j<NUM_MENU_ITEM2; j++) {
	    if (menu_item[i][j]) {
	       gtk_signal_disconnect_by_func(GTK_OBJECT(menu_item[i][j]),
					     GTK_SIGNAL_FUNC(cb_record_changed), NULL);     
	    }
	 }
      }
      for (i=0; i<NUM_PHONE_ENTRIES; i++) {
	 if (radio_button[i]) {
	    gtk_signal_disconnect_by_func(GTK_OBJECT(radio_button[i]),
					  GTK_SIGNAL_FUNC(cb_record_changed), NULL);
	 }
      }
      for (i=0; i<NUM_ADDRESS_CAT_ITEMS; i++) {
	 if (address_cat_menu_item2[i]) {
	    gtk_signal_disconnect_by_func(GTK_OBJECT(address_cat_menu_item2[i]),
					  GTK_SIGNAL_FUNC(cb_record_changed), NULL);     
	 }
      }
      if (!use_jos && (char_set == CHAR_SET_JAPANESE)) {
	 for (i=0; i<(NUM_ADDRESS_ENTRIES+NUM_ADDRESS_EXT_ENTRIES); i++) {
#ifdef ENABLE_GTK2
            g_signal_handlers_disconnect_by_func(gtk_txt_buf_address_text[i],
						 GTK_SIGNAL_FUNC(cb_record_changed), NULL);
#else
	    gtk_signal_disconnect_by_func(GTK_OBJECT(address_text[i]),
					  GTK_SIGNAL_FUNC(cb_record_changed), NULL);     
#endif
	 }
      } else {
	 for (i=0; i<NUM_ADDRESS_ENTRIES; i++) {
#ifdef ENABLE_GTK2
            g_signal_handlers_disconnect_by_func(gtk_txt_buf_address_text[i],
						 GTK_SIGNAL_FUNC(cb_record_changed), NULL);
#else
	    gtk_signal_disconnect_by_func(GTK_OBJECT(address_text[i]),
					  GTK_SIGNAL_FUNC(cb_record_changed), NULL);     
#endif
	 }
      }
      gtk_signal_disconnect_by_func(GTK_OBJECT(private_checkbox),
				    GTK_SIGNAL_FUNC(cb_record_changed), NULL);
   }
}

int address_print()
{
   long this_many;
   MyAddress *ma;
   AddressList *address_list;
   AddressList address_list1;

   get_pref(PREF_PRINT_THIS_MANY, &this_many, NULL);

   address_list=NULL;
   if (this_many==1) {
      ma = gtk_clist_get_row_data(GTK_CLIST(clist), clist_row_selected);
      if (ma < (MyAddress *)CLIST_MIN_DATA) {
	 return -1;
      }
      memcpy(&(address_list1.ma), ma, sizeof(MyAddress));
      address_list1.next=NULL;
      address_list = &address_list1;
   }
   if (this_many==2) {
      get_addresses2(&address_list, SORT_ASCENDING, 2, 2, 2, address_category);
   }
   if (this_many==3) {
      get_addresses2(&address_list, SORT_ASCENDING, 2, 2, 2, CATEGORY_ALL);
   }

   print_addresses(address_list);

   if ((this_many==2) || (this_many==3)) {
      free_AddressList(&address_list);
   }

   return 0;
}

int address_to_text(struct Address *addr, char *text, int len)
{
   g_snprintf(text, len,
	      "%s: %s\n%s: %s\n%s: %s\n%s: %s\n%s: %s\n"
	      "%s: %s\n%s: %s\n%s: %s\n%s: %s\n%s: %s\n"
	      "%s: %s\n%s: %s\n%s: %s\n%s: %s\n%s: %s\n"
	      "%s: %s\n%s: %s\n%s: %s\n%s: %s\n"
	      "%s: %d\n%s: %d\n%s: %d\n%s: %d\n%s: %d\n"
	      "%s: %d\n",
	      field_names[0], addr->entry[order[0]],
	      field_names[1], addr->entry[order[1]],
	      field_names[2], addr->entry[order[2]],
	      field_names[3], addr->entry[order[3]],
	      field_names[4], addr->entry[order[4]],
	      field_names[5], addr->entry[order[5]],
	      field_names[6], addr->entry[order[6]],
	      field_names[7], addr->entry[order[7]],
	      field_names[8], addr->entry[order[8]],
	      field_names[9], addr->entry[order[9]],
	      field_names[10], addr->entry[order[10]],
	      field_names[11], addr->entry[order[11]],
	      field_names[12], addr->entry[order[12]],
	      field_names[13], addr->entry[order[13]],
	      field_names[14], addr->entry[order[14]],
	      field_names[15], addr->entry[order[15]],
	      field_names[16], addr->entry[order[16]],
	      field_names[17], addr->entry[order[17]],
	      field_names[18], addr->entry[order[18]],
	      field_names[19], addr->phoneLabel[0],
	      field_names[20], addr->phoneLabel[1],
	      field_names[21], addr->phoneLabel[2],
	      field_names[22], addr->phoneLabel[3],
	      field_names[23], addr->phoneLabel[4],
	      field_names[24], addr->showPhone
	      );
   return 0;
}

/*
 * Start Import Code
 */
int address_import_callback(GtkWidget *parent_window, const char *file_path, int type)
{
   FILE *in;
   char text[65536];
   struct Address new_addr;
   unsigned char attrib;
   int i, ret, index;
   int import_all;
   AddressList *addrlist;
   AddressList *temp_addrlist;
   struct CategoryAppInfo cai;
   char old_cat_name[32];
   int suggested_cat_num;
   int new_cat_num;
   int priv;

   get_address_attrib(&attrib);

   in=fopen(file_path, "r");
   if (!in) {
      jp_logf(JP_LOG_WARN, _("Unable to open file: %s\n"), file_path);
      return -1;
   }

   /* CSV */
   if (type==IMPORT_TYPE_CSV) {
      jp_logf(JP_LOG_DEBUG, "Address import CSV [%s]\n", file_path);
      /* The first line is format, so we don't need it */
      fgets(text, sizeof(text), in);
      import_all=FALSE;
      while (1) {
	 /* Read the category field */
	 ret = read_csv_field(in, text, sizeof(text));
	 if (feof(in)) break;
#ifdef JPILOT_DEBUG
	 printf("category is [%s]\n", text);
#endif
	 strncpy(old_cat_name, text, 16);
	 old_cat_name[16]='\0';
	 attrib=0;
	 /* Figure out what the best category number is */
	 suggested_cat_num=0;
	 for (i=0; i<NUM_ADDRESS_CAT_ITEMS; i++) {
	    if (address_app_info.category.name[i][0]=='\0') continue;
	    if (!strcmp(address_app_info.category.name[i], old_cat_name)) {
	       suggested_cat_num=i;
	       i=1000;
	       break;
	    }
	 }

	 /* Read the private field */
	 ret = read_csv_field(in, text, sizeof(text));
#ifdef JPILOT_DEBUG
	 printf("private is [%s]\n", text);
#endif
	 sscanf(text, "%d", &priv);

	 for (i=0; i<19; i++) {
	    new_addr.entry[order[i]]=NULL;
	    ret = read_csv_field(in, text, sizeof(text));
	    new_addr.entry[order[i]]=strdup(text);
	 }
	 for (i=0; i<5; i++) {
	    ret = read_csv_field(in, text, sizeof(text));
	    sscanf(text, "%d", &(new_addr.phoneLabel[i]));
	 }
	 ret = read_csv_field(in, text, sizeof(text));
	 sscanf(text, "%d", &(new_addr.showPhone));

	 address_to_text(&new_addr, text, sizeof(text));
	 if (!import_all) {
	    ret=import_record_ask(parent_window, pane,
				  text,
				  &(address_app_info.category),
				  old_cat_name,
				  priv,
				  suggested_cat_num,
				  &new_cat_num);
	 } else {
	    new_cat_num=suggested_cat_num;
	 }
	 if (ret==DIALOG_SAID_IMPORT_QUIT) break;
	 if (ret==DIALOG_SAID_IMPORT_SKIP) continue;
	 if (ret==DIALOG_SAID_IMPORT_ALL) {
	    import_all=TRUE;
	 }
	 attrib = (new_cat_num & 0x0F) |
	   (priv ? dlpRecAttrSecret : 0);
	 if ((ret==DIALOG_SAID_IMPORT_YES) || (import_all)) {
	    pc_address_write(&new_addr, NEW_PC_REC, attrib, NULL);
	 }
      }
   }

   /* Palm Desktop DAT format */
   if (type==IMPORT_TYPE_DAT) {
      jp_logf(JP_LOG_DEBUG, "Address import DAT [%s]\n", file_path);
      if (dat_check_if_dat_file(in)!=DAT_ADDRESS_FILE) {
	 jp_logf(JP_LOG_WARN, _("File doesn't appear to be address.dat format\n"));
	 fclose(in);
	 return 1;
      }
      addrlist=NULL;
      dat_get_addresses(in, &addrlist, &cai);
      import_all=FALSE;
      for (temp_addrlist=addrlist; temp_addrlist; temp_addrlist=temp_addrlist->next) {
	 index=temp_addrlist->ma.unique_id-1;
	 if (index<0) {
	    strncpy(old_cat_name, _("Unfiled"), 16);
	    old_cat_name[16]='\0';
	    index=0;
	 } else {
	    strncpy(old_cat_name, cai.name[index], 16);
	    old_cat_name[16]='\0';
	 }
	 attrib=0;
	 /* Figure out what category it was in the dat file */
	 index=temp_addrlist->ma.unique_id-1;
	 suggested_cat_num=0;
	 if (index>-1) {
	    for (i=0; i<NUM_ADDRESS_CAT_ITEMS; i++) {
	       if (address_app_info.category.name[i][0]=='\0') continue;
	       if (!strcmp(address_app_info.category.name[i], old_cat_name)) {
		  suggested_cat_num=i;
		  i=1000;
		  break;
	       }
	    }
	 }

	 ret=0;
	 if (!import_all) {
	    address_to_text(&(temp_addrlist->ma.a), text, sizeof(text));
	    ret=import_record_ask(parent_window, pane,
				  text,
				  &(address_app_info.category),
				  old_cat_name,
				  (temp_addrlist->ma.attrib & 0x10),
				  suggested_cat_num,
				  &new_cat_num);
	 } else {
	    new_cat_num=suggested_cat_num;
	 }
	 if (ret==DIALOG_SAID_IMPORT_QUIT) break;
	 if (ret==DIALOG_SAID_IMPORT_SKIP) continue;
	 if (ret==DIALOG_SAID_IMPORT_ALL) {
	    import_all=TRUE;
	 }
	 attrib = (new_cat_num & 0x0F) |
	   ((temp_addrlist->ma.attrib & 0x10) ? dlpRecAttrSecret : 0);
	 if ((ret==DIALOG_SAID_IMPORT_YES) || (import_all)) {
	    pc_address_write(&(temp_addrlist->ma.a), NEW_PC_REC, attrib, NULL);
	 }
      }
      free_AddressList(&addrlist);
   }

   address_refresh();
   fclose(in);
   return 0;
}

int address_import(GtkWidget *window)
{
   char *type_desc[] = {
      "CSV (Comma Separated Values)",
      "DAT/ABA (Palm Archive Formats)",
      NULL
   };
   int type_int[] = {
      IMPORT_TYPE_CSV,
      IMPORT_TYPE_DAT,
      0
   };

   import_gui(window, pane, type_desc, type_int, address_import_callback);
   return 0;
}
/*
 * End Import Code
 */

/*
 * Start Export code
 */

static char *ldifMapType(int label)
{
   switch(label) {
    case 0:
      return "telephoneNumber";
    case 1:
      return "homePhone";
    case 2:
      return "facsimileTelephoneNumber";
    case 3:
      return "xotherTelephoneNumber";
    case 4:
      return "mail";
    case 5:
      return "xmainTelephoneNumber";
    case 6:
      return "pager";
    case 7:
      return "mobile";
    default:
      return "xunknownTelephoneNumber";
   }
}

static char *vCardMapType(int label)
{
   switch(label) {
    case 0:
      return "work";
    case 1:
      return "home";
    case 2:
      return "fax";
    case 3:
      return "x-other";
    case 4:
      return "email";
    case 5:
      return "x-main";
    case 6:
      return "pager";
    case 7:
      return "cell";
    default:
      return "x-unknown";
   }
}

void cb_addr_export_ok(GtkWidget *export_window, GtkWidget *clist,
		       int type, const char *filename)
{
   MyAddress *ma;
   GList *list, *temp_list;
   FILE *out;
   struct stat statb;
   const char *short_date;
   time_t ltime;
   struct tm *now;
   char str1[256], str2[256];
   char pref_time[40];
   int i, r, n, len;
   char *button_text[]={gettext_noop("OK")};
   char *button_overwrite_text[]={gettext_noop("Yes"), gettext_noop("No")};
   char text[1024];
   char csv_text[65550];
   long char_set, use_jos;
   char username[256];
   char hostname[256];
   const char *svalue;
   long userid;

   /* this stuff is for vcard only. */
   /* todo: create a pre-export switch */
   get_pref(PREF_USER, &userid, &svalue);
   strncpy(text, svalue, sizeof(text));
   text[sizeof(text)-1]='\0';
   str_to_ical_str(username, sizeof(username), text);
   get_pref(PREF_USER_ID, &userid, &svalue);
   gethostname(text, sizeof(text));
   text[sizeof(text)-1]='\0';
   str_to_ical_str(hostname, sizeof(hostname), text);

   list=GTK_CLIST(clist)->selection;

   if (!stat(filename, &statb)) {
      if (S_ISDIR(statb.st_mode)) {
	 g_snprintf(text, sizeof(text), _("%s is a directory"), filename);
	 dialog_generic(GTK_WINDOW(export_window),
			0, 0, _("Error Opening File"),
			_("Directory"), text, 1, button_text);
	 return;
      }
      g_snprintf(text, sizeof(text), _("Do you want to overwrite file %s?"), filename);
      r = dialog_generic(GTK_WINDOW(export_window),
			 0, 0, _("Overwrite File?"),
			 _("Overwrite File"), text, 2, button_overwrite_text);
      if (r!=DIALOG_SAID_1) {
	 return;
      }
   }

   out = fopen(filename, "w");
   if (!out) {
      g_snprintf(text, sizeof(text), _("Error opening file: %s"), filename);
      dialog_generic(GTK_WINDOW(export_window),
		     0, 0, _("Error Opening File"),
		     _("Filename"), text, 1, button_text);
      return;
   }

   for (i=0, temp_list=list; temp_list; temp_list = temp_list->next, i++) {
      ma = gtk_clist_get_row_data(GTK_CLIST(clist), (int) temp_list->data);
      if (!ma) {
	 continue;
	 jp_logf(JP_LOG_WARN, _("Can't export address %d\n"), (long) temp_list->data + 1);
      }
      switch (type) {
       case EXPORT_TYPE_TEXT:
	 get_pref(PREF_SHORTDATE, NULL, &short_date);
	 get_pref_time_no_secs(pref_time);
	 time(&ltime);
	 now = localtime(&ltime);
	 strftime(str1, sizeof(str1), short_date, now);
	 strftime(str2, sizeof(str2), pref_time, now);
	 g_snprintf(text, sizeof(text), "%s %s", str1, str2);

	 /* Todo Should I translate these? */
	 fprintf(out, "Address: exported from %s on %s\n", PN, text);
	 fprintf(out, "Category: %s\n", address_app_info.category.name[ma->attrib & 0x0F]);
	 fprintf(out, "Private: %s\n",
		 (ma->attrib & dlpRecAttrSecret) ? "Yes":"No");
	 for (n=0; (field_names[n]) && (n < NUM_ADDRESS_ENTRIES); n++) {
	    fprintf(out, "%s: ", field_names[n]);
	    fprintf(out, "%s\n", ma->a.entry[order[n]] ?
		    ma->a.entry[order[n]] : "");
	 }
	 for (n = 0; (field_names[n + NUM_ADDRESS_ENTRIES]) && ( n < NUM_PHONE_ENTRIES); n++) {
	    fprintf(out, "%s: ", field_names[n + NUM_ADDRESS_ENTRIES]);
	    fprintf(out, "%d\n", ma->a.phoneLabel[n]);
	 }
	 fprintf(out, "Show Phone: %d\n\n", ma->a.showPhone);
	 break;
       case EXPORT_TYPE_CSV:
	 get_pref(PREF_CHAR_SET, &char_set, NULL);
	 get_pref(PREF_USE_JOS, &use_jos, NULL);
	 if (i==0) {
	    fprintf(out, "CSV address: Category, Private, ");
	    if (!use_jos && (char_set == CHAR_SET_JAPANESE)) {
	       for (n=0; (field_names_ja[n]) 
		    && (n < NUM_ADDRESS_ENTRIES + (NUM_PHONE_ENTRIES * 2) + 1 
			+ NUM_ADDRESS_EXT_ENTRIES); n++) {
		  fprintf(out, "%s", field_names_ja[n]);
		  if (field_names_ja[n+1]) {
		     fprintf(out, ", ");
		  }
	       }
	    } else {
	       for (n=0; (field_names[n]) 
		    && (n < NUM_ADDRESS_ENTRIES + (NUM_PHONE_ENTRIES*2) +1 ); n++) {
		  fprintf(out, "%s", field_names[n]);
		  if (field_names[n+1]) {
		     fprintf(out, ", ");
		  }
	       }
	    }
	    fprintf(out, "\n");
	 }
	 len=0;
	 str_to_csv_str(csv_text,
			address_app_info.category.name[ma->attrib & 0x0F]);
	 fprintf(out, "\"%s\",", csv_text);
	 fprintf(out, "\"%s\",", (ma->attrib & dlpRecAttrSecret) ? "1":"0");
	 if (!use_jos && (char_set == CHAR_SET_JAPANESE)) {
	    char *tmp_p;
	    for (n = 0; n < NUM_ADDRESS_ENTRIES + NUM_ADDRESS_EXT_ENTRIES; n++) {
	       csv_text[0] = '\0';
	       if ((order_ja[n] < NUM_ADDRESS_EXT_ENTRIES)
		   && (tmp_p = strchr(ma->a.entry[order_ja[n]],'\1'))) {
		  if (strlen(ma->a.entry[order_ja[n]]) > 65535) {
		     jp_logf(JP_LOG_WARN, "%s > 65535\n", _("Field"));
		  } else {
		     *(tmp_p) = '\0';
		     str_to_csv_str(csv_text, ma->a.entry[order_ja[n]]);
		     *(tmp_p) = '\1';
		  }
	       } else if (order_ja[n] < NUM_ADDRESS_ENTRIES) {
		  if (strlen(ma->a.entry[i]) > 65535) {
		     jp_logf(JP_LOG_WARN, "%s > 65535\n", _("Field"));
		  } else {
		     str_to_csv_str(csv_text, ma->a.entry[order_ja[n]]);
		  }
	       } else if (ma->a.entry[order_ja[n] - NUM_ADDRESS_ENTRIES] 
			  && (tmp_p = strchr(ma->a.entry[order_ja[n] - NUM_ADDRESS_ENTRIES], '\1'))) {
		  str_to_csv_str(csv_text, (tmp_p + 1));
	       } else {
		  str_to_csv_str(csv_text, ma->a.entry[order_ja[n]]);
	       }
	       fprintf(out, "\"%s\", ", csv_text);
	    }
	 } else {
	    for (n = 0; n < NUM_ADDRESS_ENTRIES; n++) {
	       csv_text[0]='\0';
	       if (ma->a.entry[order[n]]) {
		  if (strlen(ma->a.entry[order[n]])>65535) {
		     jp_logf(JP_LOG_WARN, "%s > 65535\n", _("Field"));
		  } else {
		     str_to_csv_str(csv_text, ma->a.entry[order[n]]);
		  }
	       }
	       fprintf(out, "\"%s\",", csv_text);
	    }
	 }
	 for (n = 0; n < NUM_PHONE_ENTRIES; n++) {
	    fprintf(out, "\"%d\",", ma->a.phoneLabel[n]);
	 }
	 fprintf(out, "\"%d\"", ma->a.showPhone);
	 fprintf(out, "\n");
	 break;

       case EXPORT_TYPE_VCARD:
	 /* RFC 2426: vCard MIME Directory Profile */
	 fprintf(out, "BEGIN:VCARD\nVERSION:3.0\n");
	 fprintf(out, "PRODID:%s\n", FPI_STRING);
	 if (ma->attrib & dlpRecAttrSecret) {
	    fprintf(out, "CLASS:PRIVATE\n");
	 }
	 fprintf(out, "UID:palm-addressbook-%08x-%08lx-%s@%s\n",
		 ma->unique_id, userid, username, hostname);
	 str_to_vcard_str(csv_text, sizeof(csv_text), 
			  address_app_info.category.name[ma->attrib & 0x0F]);
	 fprintf(out, "CATEGORIES:%s\n", csv_text);
	 if (ma->a.entry[0] || ma->a.entry[1]) {
	    char *last = ma->a.entry[0];
	    char *first = ma->a.entry[1];
	    fprintf(out, "FN:");
	    if (first) {
	       str_to_vcard_str(csv_text, sizeof(csv_text), first);
	       fprintf(out, "%s", csv_text);
	    }
	    if (first && last) {
	       fprintf(out, " ");
	    }
	    if (last) {
	       str_to_vcard_str(csv_text, sizeof(csv_text), last);
	       fprintf(out, "%s", csv_text);
	    }
	    fprintf(out, "\n");
	    fprintf(out, "N:");
	    if (last) {
	       str_to_vcard_str(csv_text, sizeof(csv_text), last);
	       fprintf(out, "%s", csv_text);
	    }
	    fprintf(out, ";");
	    /* split up first into first + middle and do first;middle,middle*/
	    if (first) {
	       str_to_vcard_str(csv_text, sizeof(csv_text), first);
	       fprintf(out, "%s", csv_text);
	    }
	    fprintf(out, "\n");
	 } else if (ma->a.entry[2]) {
	    str_to_vcard_str(csv_text, sizeof(csv_text), ma->a.entry[2]);
	    fprintf(out, "FN:%s\nN:%s\n", csv_text, csv_text);
	 } else {
	    fprintf(out, "FN:-Unknown-\nN:known-;-Un\n");
	 }
	 if (ma->a.entry[13]) {
	    str_to_vcard_str(csv_text, sizeof(csv_text), ma->a.entry[13]);
	    fprintf(out, "TITLE:%s\n", csv_text);
	 }
	 if (ma->a.entry[2]) {
	    str_to_vcard_str(csv_text, sizeof(csv_text), ma->a.entry[2]);
	    fprintf(out, "ORG:%s\n", csv_text);
	 }
	 for (n = 3; n < 8; n++) {
	    if (ma->a.entry[n]) {
	       str_to_vcard_str(csv_text, sizeof(csv_text), ma->a.entry[n]);
	       if (ma->a.phoneLabel[n - 3] == 4) {
		  fprintf(out, "EMAIL:%s\n", csv_text);
	       } else {
		  fprintf(out, "TEL;TYPE=%s", vCardMapType(ma->a.phoneLabel[n - 3]));
		  if (ma->a.showPhone == n - 3) {
		     fprintf(out, ",pref");
		  }
		  fprintf(out, ":%s\n", csv_text);
	       }
	    }
	 }
	 if (ma->a.entry[8] || ma->a.entry[9] || ma->a.entry[10] || ma->a.entry[11] || ma->a.entry[12]) {
	    /* XXX wrap this line. */
	    fprintf(out, "ADR:;;");
	    for (n = 8; n < 13; n++) {
	       if (ma->a.entry[n]) {
		  str_to_vcard_str(csv_text, sizeof(csv_text), ma->a.entry[n]);
		  fprintf(out, "%s", csv_text);
	       }
	       if (n < 12) {
		  fprintf(out, ";");
	       }
	    }
	    fprintf(out, "\n");
	 }
	 if (ma->a.entry[14] || ma->a.entry[15] || ma->a.entry[16] ||
	     ma->a.entry[17] || ma->a.entry[18]) {
	    char *labels[]={"Custom1","Custom2","Custom3","Custom4","Note"};
	    int firstnote=1;
	    fprintf(out, "NOTE:");
	    for (n=14;n<=18;n++) {
	       if (ma->a.entry[n]) {
		  str_to_vcard_str(csv_text, sizeof(csv_text), ma->a.entry[n]);
		  if (firstnote == 0) {
		     fprintf(out, " ");
		  }
		  if (n==18 && firstnote) {
		     fprintf(out, "%s\\n\n", csv_text);
		  } else {
		     fprintf(out, "%s:\\n\n %s\\n\n", labels[n-14], csv_text);
		  }
		  firstnote=0;
	       }
	    }
	 }
	 fprintf(out, "END:VCARD\n");
	 break;
       case EXPORT_TYPE_LDIF:
	 /* RFC 2256 - organizationalPerson */
	 /* RFC 2798 - inetOrgPerson */
	 /* RFC 2849 - LDIF file format */
	 if (i == 0) {
	    fprintf(out, "version: 1\n");
	 }
	 {
	    char *cn;
	    char *email = NULL;
	    char *last = ma->a.entry[0];
	    char *first = ma->a.entry[1];
	    for (n = 3; n < 8; n++) {
	       if (ma->a.entry[n] && ma->a.phoneLabel[n - 3] == 4) {
		  email = ma->a.entry[n];
		  break;
	       }
	    }
	    if (first || last) {
	       cn = csv_text;
	       snprintf(csv_text, sizeof(csv_text), "%s%s%s", first ? first : "",
			first && last ? " " : "", last ? last : "");
	       if (!last) {
		  last = first;
		  first = NULL;
	       }
	    } else if (ma->a.entry[2]) {
	       last = ma->a.entry[2];
	       cn = last;
	    } else {
	       last = "Unknown";
	       cn = last;
	    }
	    /* maybe add dc=%s for each part of the email address? */
	    /* Mozilla just does mail=%s */
	    ldif_out(out, "dn", "cn=%s%s%s", cn, email ? ",mail=" : "",
		     email ? email : "");
	    fprintf(out, "dnQualifier: %s\n", PN);
	    fprintf(out, "objectClass: top\nobjectClass: person\n");
	    fprintf(out, "objectClass: organizationalPerson\n");
	    fprintf(out, "objectClass: inetOrgPerson\n");
	    ldif_out(out, "cn", "%s", cn);
	    ldif_out(out, "sn", "%s", last);
	    if (first)
	      ldif_out(out, "givenName", "%s", first);
	    if (ma->a.entry[2])
	      ldif_out(out, "o", "%s", ma->a.entry[2]);
	    for (n = 3; n < 8; n++) {
	       if (ma->a.entry[n]) {
		  ldif_out(out, ldifMapType(ma->a.phoneLabel[n - 3]), "%s", ma->a.entry[n]);
	       }
	    }
	    if (ma->a.entry[8])
	      ldif_out(out, "postalAddress", "%s", ma->a.entry[8]);
	    if (ma->a.entry[9])
	      ldif_out(out, "l", "%s", ma->a.entry[9]);
	    if (ma->a.entry[10])
	      ldif_out(out, "st", "%s", ma->a.entry[10]);
	    if (ma->a.entry[11])
	      ldif_out(out, "postalCode", "%s", ma->a.entry[11]);
	    if (ma->a.entry[12])
	      ldif_out(out, "c", "%s", ma->a.entry[12]);
	    if (ma->a.entry[13])
	      ldif_out(out, "title", "%s", ma->a.entry[13]);
	    if (ma->a.entry[14])
	      ldif_out(out, "custom1", "%s", ma->a.entry[14]);
	    if (ma->a.entry[15])
	      ldif_out(out, "custom2", "%s", ma->a.entry[15]);
	    if (ma->a.entry[16])
	      ldif_out(out, "custom3", "%s", ma->a.entry[16]);
	    if (ma->a.entry[17])
	      ldif_out(out, "custom4", "%s", ma->a.entry[17]);
	    if (ma->a.entry[18])
	      ldif_out(out, "description", "%s", ma->a.entry[18]);
/*	    if (ma->a.entry[19])
	      ldif_out(out, "seeAlso", "%s", ma->a.entry[19]);*/
	    fprintf(out, "\n");
	    break;
	 }
       default:
	 jp_logf(JP_LOG_WARN, _("Unknown export type\n"));
      }
   }

   if (out) {
      fclose(out);
   }
}


static void cb_addr_update_clist(GtkWidget *clist, int category)
{
   address_update_clist(clist, NULL, &export_address_list, category, FALSE);
}


static void cb_addr_export_done(GtkWidget *widget, const char *filename)
{
   free_AddressList(&export_address_list);

   set_pref(PREF_ADDRESS_EXPORT_FILENAME, 0, filename, TRUE);
}

int address_export(GtkWidget *window)
{
   int w, h, x, y;
   char *type_text[]={"Text", "CSV", "vCard", "ldif", NULL};
   int type_int[]={EXPORT_TYPE_TEXT, EXPORT_TYPE_CSV, EXPORT_TYPE_VCARD, EXPORT_TYPE_LDIF};

   gdk_window_get_size(window->window, &w, &h);
   gdk_window_get_root_origin(window->window, &x, &y);

#ifdef ENABLE_GTK2
   w = gtk_paned_get_position(GTK_PANED(pane));
#else
   w = GTK_PANED(pane)->handle_xpos;
#endif
   x+=40;

   export_gui(window,
              w, h, x, y, 3, sort_l,
	      PREF_ADDRESS_EXPORT_FILENAME,
	      type_text,
	      type_int,
	      cb_addr_update_clist,
	      cb_addr_export_done,
	      cb_addr_export_ok
	      );

   return 0;
}

/*
 * End Export Code
 */

static int find_sorted_cat(int cat)
{
   int i;
   for (i=0; i< NUM_ADDRESS_CAT_ITEMS; i++) {
      if (sort_l[i].cat_num==cat) {
	 return i;
      }
   }
   return 0;
}


void cb_delete_address(GtkWidget *widget,
		       gpointer   data)
{
   MyAddress *ma;
   int flag;
   int show_priv;
   long char_set; /* JPA */
   int i; /* JPA */

   ma = gtk_clist_get_row_data(GTK_CLIST(clist), clist_row_selected);
   if (ma < (MyAddress *)CLIST_MIN_DATA) {
      return;
   }
   /* JPA convert to Palm character set */
   get_pref(PREF_CHAR_SET, &char_set, NULL);
   if (char_set != CHAR_SET_LATIN1) {
      for (i=0; i<19; i++) {
	 if (ma->a.entry[i]) {
	    charset_j2p((unsigned char *)ma->a.entry[i],
			strlen(ma->a.entry[i])+1, char_set);
	 }
      }
   }

   /* Do masking like Palm OS 3.5 */
   show_priv = show_privates(GET_PRIVATES);
   if ((show_priv != SHOW_PRIVATES) &&
       (ma->attrib & dlpRecAttrSecret)) {
      return;
   }
   /* End Masking */
   flag = GPOINTER_TO_INT(data);
   if ((flag==MODIFY_FLAG) || (flag==DELETE_FLAG)) {
      delete_pc_record(ADDRESS, ma, flag);
      if (flag==DELETE_FLAG) {
	 /* when we redraw we want to go to the line above the deleted one */
	 if (clist_row_selected>0) {
	    clist_row_selected--;
	 }
      }
   }

   if (flag == DELETE_FLAG) {
      address_clist_redraw();
   }
}

void cb_undelete_address(GtkWidget *widget,
		         gpointer   data)
{
   MyAddress *maddr;
   int flag;
   int show_priv;

   maddr = gtk_clist_get_row_data(GTK_CLIST(clist), clist_row_selected);
   if (maddr < (MyAddress *)CLIST_MIN_DATA) {
      return;
   }

   /* Do masking like Palm OS 3.5 */
   show_priv = show_privates(GET_PRIVATES);
   if ((show_priv != SHOW_PRIVATES) &&
       (maddr->attrib & dlpRecAttrSecret)) {
      return;
   }
   /* End Masking */

   jp_logf(JP_LOG_DEBUG, "maddr->unique_id = %d\n",maddr->unique_id);
   jp_logf(JP_LOG_DEBUG, "maddr->rt = %d\n",maddr->rt);

   flag = GPOINTER_TO_INT(data);
   if (flag==UNDELETE_FLAG) {
      if (maddr->rt == DELETED_PALM_REC ||
	 (maddr->rt == DELETED_PC_REC))
      {
	 undelete_pc_record(ADDRESS, maddr, flag);
      }
      /* Possible later addition of undelete for modified records 
      else if (maddr->rt == MODIFIED_PALM_REC)
      {
	 cb_add_new_record(widget, GINT_TO_POINTER(COPY_FLAG));
      }
      */
   }

   address_clist_redraw();
}
void cb_resort(GtkWidget *widget,
	       gpointer   data)
{
   int by_company;
   MyAddress *ma;

   by_company=GPOINTER_TO_INT(data);

   if (sort_override) {
      sort_override=0;
   } else {
      sort_override=1;
      by_company=!(by_company & 1);
   }

   /* Return to this record after resorting */
   ma = gtk_clist_get_row_data(GTK_CLIST(clist), clist_row_selected);
   if (ma < (MyAddress *)CLIST_MIN_DATA) {
      glob_find_id = 0;
   }
   else {
      glob_find_id = ma->unique_id;
   }

   address_clist_redraw();
   address_find();

   /* Update labels after redrawing clist to work around GTK bug */
   if (by_company) {
      gtk_clist_set_column_title(GTK_CLIST(clist), ADDRESS_NAME_COLUMN, _("Company/Name"));
   } else {
      gtk_clist_set_column_title(GTK_CLIST(clist), ADDRESS_NAME_COLUMN, _("Name/Company"));
   }
}


void cb_phone_menu(GtkWidget *item, unsigned int value)
{
   if (!item)
     return;
   if ((GTK_CHECK_MENU_ITEM(item))->active) {
      jp_logf(JP_LOG_DEBUG, "phone_menu = %d\n", (value & 0xF0) >> 4);
      jp_logf(JP_LOG_DEBUG, "selection = %d\n", value & 0x0F);
      address_phone_label_selected[(value & 0xF0) >> 4] = value & 0x0F;
   }
}

void cb_notebook_changed(GtkWidget *widget,
			 GtkWidget *widget2,
			 int        page,
			 gpointer   data)
{
   int prev_page;

   /* GTK calls this function while it is destroying the notebook
    * I use this function to tell if it is being destroyed */
   prev_page = gtk_notebook_get_current_page(GTK_NOTEBOOK(widget));
   if (prev_page<0) {
      return;
   }
   jp_logf(JP_LOG_DEBUG, "cb_notebook_changed(), prev_page=%d, page=%d\n", prev_page, page);
   set_pref(PREF_ADDRESS_NOTEBOOK_PAGE, page, NULL, TRUE);
}

static void get_address_attrib(unsigned char *attrib)
{
   int i;
   /*Get the category that is set from the menu */
   *attrib = 0;
   for (i=0; i<NUM_ADDRESS_CAT_ITEMS; i++) {
      if (GTK_IS_WIDGET(address_cat_menu_item2[i])) {
	 if (GTK_CHECK_MENU_ITEM(address_cat_menu_item2[i])->active) {
	    *attrib = sort_l[i].cat_num;
	    break;
	 }
      }
   }
   /* Get private flag */
   if (GTK_TOGGLE_BUTTON(private_checkbox)->active) {
      *attrib |= dlpRecAttrSecret;
   }
}

static void cb_add_new_record(GtkWidget *widget,
			      gpointer   data)
{
   int i;
   struct Address a;
   MyAddress *ma;
   unsigned char attrib;
   unsigned int unique_id;
   int show_priv;
   char *str0, *str1, *str2;
   long use_jos, char_set;
#ifdef ENABLE_GTK2
   GtkTextIter start_iter;
   GtkTextIter end_iter;
#endif

   memset(&a, 0, sizeof(a));
   unique_id=0;
   ma=NULL;

   /* Do masking like Palm OS 3.5 */
   if ((GPOINTER_TO_INT(data)==COPY_FLAG) || 
       (GPOINTER_TO_INT(data)==MODIFY_FLAG)) {
      show_priv = show_privates(GET_PRIVATES);
      ma = gtk_clist_get_row_data(GTK_CLIST(clist), clist_row_selected);
      if (ma < (MyAddress *)CLIST_MIN_DATA) {
	 return;
      }
      if ((show_priv != SHOW_PRIVATES) &&
	  (ma->attrib & dlpRecAttrSecret)) {
	 return;
      }
   }
   /* End Masking */
   if ((GPOINTER_TO_INT(data)==NEW_FLAG) || 
       (GPOINTER_TO_INT(data)==COPY_FLAG) ||
       (GPOINTER_TO_INT(data)==MODIFY_FLAG)) {
      /*These rec_types are both the same for now */
      if (GPOINTER_TO_INT(data)==MODIFY_FLAG) {
	 ma = gtk_clist_get_row_data(GTK_CLIST(clist), clist_row_selected);
	 unique_id=ma->unique_id;
	 if (ma < (MyAddress *)CLIST_MIN_DATA) {
	    return;
	 }
	 if ((ma->rt==DELETED_PALM_REC) || 
	     (ma->rt==DELETED_PC_REC)   ||
	     (ma->rt==MODIFIED_PALM_REC)) {
	    jp_logf(JP_LOG_INFO, _("You can't modify a record that is deleted\n"));
	    return;
	 }
      }
      a.showPhone=0;
      for (i=0; i<NUM_PHONE_ENTRIES; i++) {
	 if (GTK_TOGGLE_BUTTON(radio_button[i])->active) {
	    a.showPhone=i;
	 }
      }
      get_pref(PREF_CHAR_SET, &char_set, NULL);
      get_pref(PREF_USE_JOS, &use_jos, NULL);
      if (!use_jos && (char_set == CHAR_SET_JAPANESE)) {
	 i=0;
	 while (i<NUM_ADDRESS_EXT_ENTRIES) {
#ifdef ENABLE_GTK2
	    gtk_text_buffer_get_bounds(GTK_TEXT_BUFFER(gtk_txt_buf_address_text[i]),&start_iter,&end_iter);
	    str1 = gtk_text_buffer_get_text(GTK_TEXT_BUFFER(gtk_txt_buf_address_text[i]),&start_iter,&end_iter,TRUE);
	    gtk_text_buffer_get_bounds(GTK_TEXT_BUFFER(address_text[i+NUM_ADDRESS_ENTRIES]),&start_iter,&end_iter);
	    str2 = gtk_text_buffer_get_text(GTK_TEXT_BUFFER(address_text[i+NUM_ADDRESS_ENTRIES]),&start_iter,&end_iter,TRUE);
#else
	    str1 = gtk_editable_get_chars(GTK_EDITABLE(address_text[i]), 0, -1);
	    str2 = gtk_editable_get_chars(GTK_EDITABLE(address_text[i+NUM_ADDRESS_ENTRIES]), 0, -1);
#endif
	    if ((str0 = (char *)malloc(strlen(str1)+strlen(str2)+2))!=NULL) {
	       if (*str1 !='\0') {
		  strcpy(str0, str1);strcat(str0,"\1");strcat(str0, str2);
	       } else {
		  strcpy(str0,str2);
	       }
	       a.entry[i] = str0;
	       free(str1);free(str2);
	    }
	    i++;
	 } 
	 while (i<NUM_ADDRESS_ENTRIES) {
#ifdef ENABLE_GTK2
	    gtk_text_buffer_get_bounds(GTK_TEXT_BUFFER(gtk_txt_buf_address_text[i]),&start_iter,&end_iter);
	    a.entry[i] =
	      gtk_text_buffer_get_text(GTK_TEXT_BUFFER(gtk_txt_buf_address_text[i]),&start_iter,&end_iter,TRUE);
#else
	    a.entry[i] =
	      gtk_editable_get_chars(GTK_EDITABLE(address_text[i]), 0, -1);
#endif
	    i++;
	 }
      } else {
	 for (i=0; i<NUM_ADDRESS_ENTRIES; i++) {
#ifdef ENABLE_GTK2
	    gtk_text_buffer_get_bounds(GTK_TEXT_BUFFER(gtk_txt_buf_address_text[i]),&start_iter,&end_iter);
	    a.entry[i] =
	      gtk_text_buffer_get_text(GTK_TEXT_BUFFER(gtk_txt_buf_address_text[i]),&start_iter,&end_iter,TRUE);
#else
	    a.entry[i] =
	      gtk_editable_get_chars(GTK_EDITABLE(address_text[i]), 0, -1);
#endif
	 }
      }
      for (i=0; i<NUM_PHONE_ENTRIES; i++) {
	 a.phoneLabel[i]=address_phone_label_selected[i];
      }

      /*Get the attributes */
      get_address_attrib(&attrib);

      set_new_button_to(CLEAR_FLAG);

      if (GPOINTER_TO_INT(data) == MODIFY_FLAG) {
	 cb_delete_address(NULL, data);
	 if ((ma->rt==PALM_REC) || (ma->rt==REPLACEMENT_PALM_REC)) {
	    pc_address_write(&a, REPLACEMENT_PALM_REC, attrib, &unique_id);
	 } else {
	    unique_id=0;
	    pc_address_write(&a, NEW_PC_REC, attrib, &unique_id);
	 }
      } else {
	 unique_id=0;
	 pc_address_write(&a, NEW_PC_REC, attrib, &unique_id);
      }
      address_clist_redraw();
      free_Address(&a);
      glob_find_id = unique_id;
      address_find();
   }
}

void addr_clear_details()
{
   int i;
   int new_cat;
   int sorted_position;
   long use_jos, char_set;

   /* Need to disconnect these signals first */
   connect_changed_signals(DISCONNECT_SIGNALS);

   /* Clear the quickview */
#ifdef ENABLE_GTK2
   gtk_text_buffer_set_text(GTK_TEXT_BUFFER(gtk_txt_buf_text), "", -1);
#else
   gtk_text_set_point(GTK_TEXT(text), 0);
   gtk_text_forward_delete(GTK_TEXT(text),
			   gtk_text_get_length(GTK_TEXT(text)));
#endif

   /*Clear all the address entry texts */
   get_pref(PREF_CHAR_SET, &char_set, NULL);
   get_pref(PREF_USE_JOS, &use_jos, NULL);
   if (!use_jos && (char_set == CHAR_SET_JAPANESE)) {
      for (i=0; i<(NUM_ADDRESS_ENTRIES+NUM_ADDRESS_EXT_ENTRIES); i++) {
#ifdef ENABLE_GTK2
	 gtk_text_buffer_set_text(GTK_TEXT_BUFFER(gtk_txt_buf_address_text[i]), "", -1);
#else
	 gtk_text_set_point(GTK_TEXT(address_text[i]), 0);
	 gtk_text_forward_delete(GTK_TEXT(address_text[i]),
				 gtk_text_get_length(GTK_TEXT(address_text[i])));
#endif
      }
   } else {
      for (i=0; i<NUM_ADDRESS_ENTRIES; i++) {
#ifdef ENABLE_GTK2
	 gtk_text_buffer_set_text(GTK_TEXT_BUFFER(gtk_txt_buf_address_text[i]), "", -1);
#else
	 gtk_text_set_point(GTK_TEXT(address_text[i]), 0);
	 gtk_text_forward_delete(GTK_TEXT(address_text[i]),
				 gtk_text_get_length(GTK_TEXT(address_text[i])));
#endif
      }
   }
   for (i=0; i<NUM_PHONE_ENTRIES; i++) {
      if (GTK_IS_WIDGET(menu_item[i][i])) {
	 gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM
					(menu_item[i][i]), TRUE);
	 gtk_option_menu_set_history(GTK_OPTION_MENU(phone_list_menu[i]), i);
      }
   }
   if (address_category==CATEGORY_ALL) {
      new_cat = 0;
   } else {
      new_cat = address_category;
   }
   sorted_position = find_sorted_cat(new_cat);
   if (sorted_position<0) {
      jp_logf(JP_LOG_WARN, _("Category is not legal\n"));
   } else {
      gtk_check_menu_item_set_active
	(GTK_CHECK_MENU_ITEM(address_cat_menu_item2[sorted_position]), TRUE);
      gtk_option_menu_set_history(GTK_OPTION_MENU(category_menu2), sorted_position);
   }

   gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(private_checkbox), FALSE);
   set_new_button_to(CLEAR_FLAG);

   connect_changed_signals(CONNECT_SIGNALS);
}

void cb_address_clear(GtkWidget *widget,
		      gpointer   data)
{
   addr_clear_details();
   gtk_notebook_set_page(GTK_NOTEBOOK(notebook), 0);
   gtk_widget_grab_focus(GTK_WIDGET(address_text[0]));
   set_new_button_to(NEW_FLAG);
}

/* Attempt to make the best possible string out of whatever garbage we find
 * Remove illegal characters, stop at carriage return and at least 1 digit
 */
void parse_phone_str(char *dest, char *src, int max_len)
{
   int i1, i2;

   for (i1=0, i2=0; (i1<max_len) && src[i1]; i1++) {
      if (isdigit(src[i1]) || (src[i1]==',')
	  || (src[i1]=='A') || (src[i1]=='B') || (src[i1]=='C')
	  || (src[i1]=='D') || (src[i1]=='*') || (src[i1]=='#')
	  ) {
	 dest[i2]=src[i1];
	 i2++;
      } else if (((src[i1] =='\n') || (src[i1] =='\r') ||
		  (src[i1] =='x')) && i2) {
	 break;
      }
   }
   dest[i2]='\0';
}

void email_contact(GtkWidget *widget, gchar *str)
{
   char command[1024];
   const char *pref_command;
   
   get_pref(PREF_MAIL_COMMAND, NULL, &pref_command);
   if (!pref_command) {
      return;
   }

   /* Make a system call command string */
   g_snprintf(command, sizeof(command), pref_command, str);
   command[1023]='\0';

   jp_logf(JP_LOG_STDOUT|JP_LOG_FILE, "executing command = [%s]\n", command);
   system(command);
}

void dial_contact(GtkWidget *widget, gchar *str)
{
   char *Px;
   char number[100];
   char ext[100];

   number[0]=ext[0]='\0';

   parse_phone_str(number, str, sizeof(number));

   Px = strstr(str, "x");
   if (Px) {
      parse_phone_str(ext, Px, sizeof(ext));
   }

   dialog_dial(GTK_WINDOW(gtk_widget_get_toplevel(widget)), number, ext);
}

void cb_dial_or_mail(GtkWidget *widget, gpointer data)
{
   GtkWidget *text;
   gchar *str;
#ifdef ENABLE_GTK2
   GtkTextIter    start_iter;
   GtkTextIter    end_iter;
   GtkTextBuffer *text_buffer;
#endif
   text=data;

#ifdef ENABLE_GTK2
   text_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text));
   gtk_text_buffer_get_bounds(text_buffer,&start_iter,&end_iter);
   str = gtk_text_buffer_get_text(text_buffer,&start_iter,&end_iter,TRUE);
#else
   str=gtk_editable_get_chars(GTK_EDITABLE(text), 0, -1);
#endif

   if (!str) return;
   printf("[%s]\n", str);

   if (strstr(str,"@")) {
      email_contact(widget, str);
   }
   else {
      dial_contact(widget, str);
   }

   g_free(str);
}

void cb_address_quickfind(GtkWidget *widget,
			  gpointer   data)
{
   const char *entry_text;
   int i, r, found, found_at, line_count;
   char *clist_text;

   found_at = 0;
   entry_text = gtk_entry_get_text(GTK_ENTRY(widget));
   if (!strlen(entry_text)) {
      return;
   }
   /* 100000 is just paranoia, shouldn't happen */
   for (found = i = 0; i<100000; i++) {
      r = gtk_clist_get_text(GTK_CLIST(clist), i, ADDRESS_NAME_COLUMN, &clist_text);
      if (!r) {
	 break;
      }
      if (found) {
	 continue;
      }
      if (!strncasecmp(clist_text, entry_text, strlen(entry_text))) {
	 found = 1;
	 found_at = i;
	 gtk_clist_select_row(GTK_CLIST(clist), i, ADDRESS_NAME_COLUMN);
      }
   }
   line_count = i;

   if (found) {
      gtk_clist_moveto(GTK_CLIST(clist), found_at, 0, 0.5, 0.0);
   }
}

static void cb_category(GtkWidget *item, int selection)
{
   int b;

   if (!item) return;
   if ((GTK_CHECK_MENU_ITEM(item))->active) {
      b=dialog_save_changed_record(pane, record_changed);
      if (b==DIALOG_SAID_1) {
	 cb_add_new_record(NULL, GINT_TO_POINTER(record_changed));
      }

      address_category = selection;
      clist_row_selected = 0;
      jp_logf(JP_LOG_DEBUG, "address_category = %d\n",address_category);
      address_update_clist(clist, category_menu1, &glob_address_list,
			   address_category, TRUE);
   }
}


/* Do masking like Palm OS 3.5 */
static void clear_myaddress(MyAddress *ma)
{
   int i;

   ma->unique_id=0;
   ma->attrib=ma->attrib & 0xF8;
   for (i=0; i<5; i++) {
      ma->a.phoneLabel[i]=0;
   }
   ma->a.showPhone=0;
   for (i=0; i<19; i++) {
      if (ma->a.entry) {
	 free(ma->a.entry[i]);
	 ma->a.entry[i]=NULL;
      }
   }
   return;
}
/* End Masking */

static void cb_edit_cats(GtkWidget *widget, gpointer data)
{
   struct AddressAppInfo ai;
   char full_name[FILENAME_MAX];
   char buffer[65536];
   int num;
   int size;
   void *buf;
   struct pi_file *pf;

   jp_logf(JP_LOG_DEBUG, "cb_edit_cats\n");

   get_home_file_name("AddressDB.pdb", full_name, sizeof(full_name));

   buf=NULL;
   memset(&ai, 0, sizeof(ai));

   pf = pi_file_open(full_name);
   pi_file_get_app_info(pf, &buf, &size);

   num = unpack_AddressAppInfo(&ai, buf, size);
   if (num <= 0) {
      jp_logf(JP_LOG_WARN, _("Error reading file: %s\n"), "AddressDB.pdb");
      return;
   }

   pi_file_close(pf);

   edit_cats(widget, "AddressDB", &(ai.category));

   size = pack_AddressAppInfo(&ai, (unsigned char*)buffer, sizeof(buffer));

   pdb_file_write_app_block("AddressDB", buffer, size);

   cb_app_button(NULL, GINT_TO_POINTER(REDRAW));
}

static void cb_clist_selection(GtkWidget      *clist,
			       gint           row,
			       gint           column,
			       GdkEventButton *event,
			       gpointer       data)
{
   /* The rename-able phone entries are indexes 3,4,5,6,7 */
   struct Address *a;
   MyAddress *ma;
   int cat, count, sorted_position;
   int i, i2;
   int keep, b;
   char *tmp_p;
   char *clist_text;
   const char *entry_text;
   long use_jos, char_set;

   if ((!event) && (column < 0)) return;
   if ((!event) && (clist_hack)) return;

   /* HACK, see clist hack explanation in memo_gui.c */
   if (clist_hack) {
      keep=record_changed;
      gtk_clist_select_row(GTK_CLIST(clist), clist_row_selected, column);
      b=dialog_save_changed_record(pane, record_changed);
      if (b==DIALOG_SAID_1) {
	 cb_add_new_record(NULL, GINT_TO_POINTER(record_changed));
      }
      set_new_button_to(CLEAR_FLAG);
      gtk_clist_select_row(GTK_CLIST(clist), row, column);
      return;
   }

   clist_row_selected=row;

   ma = gtk_clist_get_row_data(GTK_CLIST(clist), row);
   if (ma==NULL) {
      return;
   }

   if (ma->rt == DELETED_PALM_REC ||
      (ma->rt == DELETED_PC_REC))
      /* Possible later addition of undelete code for modified deleted records
         || ma->rt == MODIFIED_PALM_REC
      */
   {
      set_new_button_to(UNDELETE_FLAG);
   }
   else
   {
      set_new_button_to(CLEAR_FLAG);
   }

   connect_changed_signals(DISCONNECT_SIGNALS);

   a=&(ma->a);
   clist_text = NULL;
   gtk_clist_get_text(GTK_CLIST(clist), row, ADDRESS_NAME_COLUMN, &clist_text);
   entry_text = gtk_entry_get_text(GTK_ENTRY(address_quickfind_entry));
   if (strncasecmp(clist_text, entry_text, strlen(entry_text))) {
      gtk_entry_set_text(GTK_ENTRY(address_quickfind_entry), "");
   }

#ifdef ENABLE_GTK2
   gtk_widget_freeze_child_notify(text);

   gtk_text_buffer_set_text(GTK_TEXT_BUFFER(gtk_txt_buf_text), "", -1);
#else
   gtk_text_freeze(GTK_TEXT(text));

   gtk_text_set_point(GTK_TEXT(text), 0);
   gtk_text_forward_delete(GTK_TEXT(text),
			   gtk_text_get_length(GTK_TEXT(text)));
#endif

   gtk_text_insert(GTK_TEXT(text), NULL,NULL,NULL, _("Category: "), -1);
   gtk_text_insert(GTK_TEXT(text), NULL,NULL,NULL,
		   address_app_info.category.name[ma->attrib & 0x0F], -1);
   gtk_text_insert(GTK_TEXT(text), NULL,NULL,NULL, "\n", -1);

   get_pref(PREF_CHAR_SET, &char_set, NULL);
   get_pref(PREF_USE_JOS, &use_jos, NULL);
   if (!use_jos && (char_set == CHAR_SET_JAPANESE)) {
      for (i=0; i<(NUM_ADDRESS_ENTRIES+NUM_ADDRESS_EXT_ENTRIES); i++) {
	 i2 = order_ja[i];
	 if (i2<NUM_ADDRESS_ENTRIES) {
	    if (a->entry[i2]) {
	       if (i2>2 && i2<8) {
		  gtk_text_insert(GTK_TEXT(text), NULL,NULL,NULL,
				  address_app_info.phoneLabels[a->phoneLabel[i2-3]], -1);
	       } else {
		  gtk_text_insert(GTK_TEXT(text), NULL,NULL,NULL, address_app_info.labels[i2], -1); 
	       }
	       gtk_text_insert(GTK_TEXT(text), NULL,NULL,NULL, ": ", -1);

	       if ((tmp_p = strchr(a->entry[i2],'\1'))) {
		  *(tmp_p) = '\0';
		  gtk_text_insert(GTK_TEXT(text), NULL,NULL,NULL, a->entry[i2], -1); 
		  *(tmp_p) = '\1';
	       } else {
		  gtk_text_insert(GTK_TEXT(text), NULL,NULL,NULL, a->entry[i2], -1); 
	       }
	       gtk_text_insert(GTK_TEXT(text), NULL,NULL,NULL, "\n", -1);
	    }
	 } else {
	    if (a->entry[i2-NUM_ADDRESS_ENTRIES]) {    
	       if ((tmp_p = strchr(a->entry[i2-NUM_ADDRESS_ENTRIES],'\1'))) {
		  gtk_text_insert(GTK_TEXT(text), NULL,NULL,NULL, _("kana("), -1); 
		  gtk_text_insert(GTK_TEXT(text), NULL,NULL,NULL, 
				  address_app_info.labels[i2-NUM_ADDRESS_ENTRIES], -1); 
		  gtk_text_insert(GTK_TEXT(text), NULL,NULL,NULL, "):", -1); 
		  gtk_text_insert(GTK_TEXT(text), NULL,NULL,NULL, tmp_p+1, -1); 
		  gtk_text_insert(GTK_TEXT(text), NULL,NULL,NULL, "\n", -1);
	       }
	    }
	 }
      }
   } else {
      for (i=0; i<NUM_ADDRESS_ENTRIES; i++) {
	 i2=order[i];
	 if (a->entry[i2]) {
	    if (i2>2 && i2<8) {
	       gtk_text_insert(GTK_TEXT(text), NULL,NULL,NULL,
			       address_app_info.phoneLabels[a->phoneLabel[i2-3]],
			       -1);
	    } else {
	       gtk_text_insert(GTK_TEXT(text), NULL,NULL,NULL, address_app_info.labels[i2], -1);
	    }
	    gtk_text_insert(GTK_TEXT(text), NULL,NULL,NULL, ": ", -1);
	    gtk_text_insert(GTK_TEXT(text), NULL,NULL,NULL, a->entry[i2], -1);
	    gtk_text_insert(GTK_TEXT(text), NULL,NULL,NULL, "\n", -1);
	 }
      }
   }
#ifdef ENABLE_GTK2
   gtk_widget_thaw_child_notify(text);
#else
   gtk_text_thaw(GTK_TEXT(text));
#endif

   cat = ma->attrib & 0x0F;
   sorted_position = find_sorted_cat(cat);
   if (address_cat_menu_item2[sorted_position]==NULL) {
      /* Illegal category, Assume that category 0 is Unfiled and valid*/
      jp_logf(JP_LOG_DEBUG, "Category is not legal\n");
      cat = sorted_position = 0;
      sorted_position = find_sorted_cat(cat);
   }
   /* We need to count how many items down in the list this is */
   for (i=sorted_position, count=0; i>=0; i--) {
      if (address_cat_menu_item2[i]) {
	 count++;
      }
   }
   count--;

   if (sorted_position<0) {
      jp_logf(JP_LOG_WARN, _("Category is not legal\n"));
   } else {
      if (address_cat_menu_item2[sorted_position]) {
	 gtk_check_menu_item_set_active
	   (GTK_CHECK_MENU_ITEM(address_cat_menu_item2[sorted_position]), TRUE);
      }
      gtk_option_menu_set_history(GTK_OPTION_MENU(category_menu2), count);
   }

   get_pref(PREF_CHAR_SET, &char_set, NULL);
   get_pref(PREF_USE_JOS, &use_jos, NULL);
   if (!use_jos && (char_set == CHAR_SET_JAPANESE)) {
      for (i=0; i<(NUM_ADDRESS_ENTRIES+NUM_ADDRESS_EXT_ENTRIES); i++) {
#ifdef ENABLE_GTK2
	 gtk_text_buffer_set_text(GTK_TEXT_BUFFER(gtk_txt_buf_address_text[i]), "", -1);
#else
	 gtk_text_set_point(GTK_TEXT(address_text[i]), 0); 
	 gtk_text_forward_delete(GTK_TEXT(address_text[i]),
				 gtk_text_get_length(GTK_TEXT(address_text[i])));
#endif
	 if (i<NUM_ADDRESS_EXT_ENTRIES){
	    if (a->entry[i]) {
	       if ((tmp_p = strchr(a->entry[i],'\1'))) {
		  *(tmp_p) = '\0';
		  gtk_text_insert(GTK_TEXT(address_text[i]), NULL,NULL,NULL, a->entry[i], -1); 
		  *(tmp_p) = '\1';
	       } else {
		  gtk_text_insert(GTK_TEXT(address_text[i]), NULL,NULL,NULL, a->entry[i], -1); 
	       }
	    }
	 } else if (i < NUM_ADDRESS_ENTRIES){
	    if (a->entry[i]) {
	       gtk_text_insert(GTK_TEXT(address_text[i]), NULL,NULL,NULL, a->entry[i], -1); 
	    }
	 } else {
	    if (a->entry[i-NUM_ADDRESS_ENTRIES]) {
	       if ((tmp_p = strchr(a->entry[i-NUM_ADDRESS_ENTRIES],'\1'))) {
		  gtk_text_insert(GTK_TEXT(address_text[i]), NULL,NULL,NULL, tmp_p+1, -1); 
	       }
	    }
	 }
      }
   } else {
      for (i=0; i<NUM_ADDRESS_ENTRIES; i++) {
#ifdef ENABLE_GTK2
	 gtk_text_buffer_set_text(GTK_TEXT_BUFFER(gtk_txt_buf_address_text[i]), "", -1);
#else
	 gtk_text_set_point(GTK_TEXT(address_text[i]), 0); 
	 gtk_text_forward_delete(GTK_TEXT(address_text[i]),
				 gtk_text_get_length(GTK_TEXT(address_text[i])));
#endif
	 if (a->entry[i]) {
	    gtk_text_insert(GTK_TEXT(address_text[i]), NULL,NULL,NULL, a->entry[i], -1);
	 }
      }
   }
   for (i=0; i<NUM_PHONE_ENTRIES; i++) {
      if (GTK_IS_WIDGET(menu_item[i][a->phoneLabel[i]])) {
	 gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM
					(menu_item[i][a->phoneLabel[i]]), TRUE);
	 gtk_option_menu_set_history(GTK_OPTION_MENU(phone_list_menu[i]),
				     a->phoneLabel[i]);
	 if (!strcmp(address_app_info.labels[a->phoneLabel[i]+3],_("E-mail")))
	 {
#ifdef ENABLE_GTK2
	    gtk_button_set_label(GTK_BUTTON(dial_button[i]),_("Mail"));
#else
	    gtk_object_set(GTK_OBJECT(dial_button[i]), "label", _("Mail"), NULL);
#endif
	 }
	 else
	 {
#ifdef ENABLE_GTK2
	    gtk_button_set_label(GTK_BUTTON(dial_button[i]),_("Dial"));
#else
	    gtk_object_set(GTK_OBJECT(dial_button[i]), "label", _("Dial"), NULL);
#endif
	 }
      }
   }
   if ((a->showPhone > -1) && (a->showPhone < NUM_PHONE_ENTRIES)) {
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(radio_button[a->showPhone]),
				   TRUE);
   }
   gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(private_checkbox),
				ma->attrib & dlpRecAttrSecret);

   connect_changed_signals(CONNECT_SIGNALS);
}

static void address_update_clist(GtkWidget *clist, GtkWidget *tooltip_widget,
				 AddressList **addr_list, int category, int main)
{
   int num_entries, entries_shown, i;
   int show1, show2, show3;
   gchar *empty_line[] = { "","","" };
   GdkPixmap *pixmap_note;
   GdkBitmap *mask_note;
   AddressList *temp_al;
   char str[ADDRESS_MAX_CLIST_NAME+8];
   char str2[ADDRESS_MAX_COLUMN_LEN+2];
   int by_company;
   int show_priv;
   long use_jos, char_set;
   char *tmp_p1, *tmp_p2, *tmp_p3;
   char blank[]="";
   char slash[]=" / ";
   char comma_space[]=", ";
   char *field1, *field2, *field3;
   char *delim1, *delim2;
   char *tmp_delim1, *tmp_delim2;

   free_AddressList(addr_list);

#ifdef JPILOT_DEBUG
    for (i=0;i<NUM_ADDRESS_CAT_ITEMS;i++) {
      jp_logf(JP_LOG_DEBUG, "renamed:[%02d]:\n",address_app_info.category.renamed[i]);
      jp_logf(JP_LOG_DEBUG, "category name:[%02d]:",i);
      print_string(address_app_info.category.name[i],16);
      jp_logf(JP_LOG_DEBUG, "category ID:%d\n", address_app_info.category.ID[i]);
   }

   for (i=0;i<NUM_ADDRESS_LABELS;i++) {
      jp_logf(JP_LOG_DEBUG, "labels[%02d]:",i);
      print_string(address_app_info.labels[i],16);
   }
   for (i=0;i<8;i++) {
      jp_logf(JP_LOG_DEBUG, "phoneLabels[%d]:",i);
      print_string(address_app_info.phoneLabels[i],16);
   }
   jp_logf(JP_LOG_DEBUG, "country %d\n",address_app_info.country);
   jp_logf(JP_LOG_DEBUG, "sortByCompany %d\n",address_app_info.sortByCompany);
#endif

   /* Need to get all records including private ones for the tooltips calculation */
   num_entries = get_addresses2(addr_list, SORT_ASCENDING, 2, 2, 1, CATEGORY_ALL);

   /* Start by clearing existing entry if in main window */
   if (main) {
      addr_clear_details();
   }


   /*Clear the text box to make things look nice */
   if (main) {
#ifdef ENABLE_GTK2
      gtk_text_buffer_set_text(GTK_TEXT_BUFFER(gtk_txt_buf_text), "", -1);
#else
      gtk_text_set_point(GTK_TEXT(text), 0);
      gtk_text_forward_delete(GTK_TEXT(text),
			      gtk_text_get_length(GTK_TEXT(text)));
#endif
   }

   /* Freeze clist to prevent flicker during updating */
   gtk_clist_freeze(GTK_CLIST(clist));
   gtk_clist_clear(GTK_CLIST(clist));

   /* Collect preferences and pixmaps before loop */
   get_pref(PREF_CHAR_SET, &char_set, NULL);
   get_pref(PREF_USE_JOS, &use_jos, NULL);
   show_priv = show_privates(GET_PRIVATES);
   get_pixmaps(clist, PIXMAP_NOTE, &pixmap_note, &mask_note);

   by_company = address_app_info.sortByCompany;
   if (sort_override) {
      by_company=!(by_company & 1);
   }
   if (by_company) {
      show1=2; /*company */
      show2=0; /*last name */
      show3=1; /*first name */
      delim1 = slash;
      delim2 = comma_space;
   } else {
      show1=0; /*last name */
      show2=1; /*first name */
      show3=2; /*company */
      delim1 = comma_space;
      delim2 = slash;
   }

   entries_shown=0;

   for (temp_al = *addr_list, i=0; temp_al; temp_al=temp_al->next) {
      if ( ((temp_al->ma.attrib & 0x0F) != address_category) &&
	   address_category != CATEGORY_ALL) {
	 continue;
      }
      /* Do masking like Palm OS 3.5 */
      if ((show_priv == MASK_PRIVATES) && 
	  (temp_al->ma.attrib & dlpRecAttrSecret)) {
	 gtk_clist_append(GTK_CLIST(clist), empty_line);
	 gtk_clist_set_text(GTK_CLIST(clist), entries_shown, ADDRESS_NAME_COLUMN, "---------------");
	 gtk_clist_set_text(GTK_CLIST(clist), entries_shown, ADDRESS_PHONE_COLUMN, "---------------");
	 clear_myaddress(&temp_al->ma);
	 gtk_clist_set_row_data(GTK_CLIST(clist), entries_shown, &(temp_al->ma));
	 gtk_clist_set_row_style(GTK_CLIST(clist), entries_shown, NULL);
	 entries_shown++;
	 continue;
      }
      /* End Masking */

      /* Hide the private records if need be */
      if ((show_priv != SHOW_PRIVATES) && 
	  (temp_al->ma.attrib & dlpRecAttrSecret)) {
	 continue;
      }

      if (!use_jos && (char_set == CHAR_SET_JAPANESE)) {
	 str[0]='\0';
	 if (temp_al->ma.a.entry[show1] || temp_al->ma.a.entry[show2]) {
	    if (temp_al->ma.a.entry[show1] && temp_al->ma.a.entry[show2]) {
	       if ((tmp_p1 = strchr(temp_al->ma.a.entry[show1],'\1'))) *tmp_p1='\0';
	       if ((tmp_p2 = strchr(temp_al->ma.a.entry[show2],'\1'))) *tmp_p2='\0';
	       g_snprintf(str, ADDRESS_MAX_CLIST_NAME, "%s, %s", temp_al->ma.a.entry[show1], temp_al->ma.a.entry[show2]);
	       if (tmp_p1) *tmp_p1='\1';
	       if (tmp_p2) *tmp_p2='\1';
	    }
	    if (temp_al->ma.a.entry[show1] && ! temp_al->ma.a.entry[show2]) {
	       if ((tmp_p1 = strchr(temp_al->ma.a.entry[show1],'\1'))) *tmp_p1='\0';
	       if (temp_al->ma.a.entry[show3]) {
		  if ((tmp_p3 = strchr(temp_al->ma.a.entry[show3],'\1'))) *tmp_p3='\0';
		  g_snprintf(str, ADDRESS_MAX_CLIST_NAME, "%s, %s", temp_al->ma.a.entry[show1], temp_al->ma.a.entry[show3]);
		  if (tmp_p3) *tmp_p3='\1';
	       } else {
		  multibyte_safe_strncpy(str, temp_al->ma.a.entry[show1], ADDRESS_MAX_CLIST_NAME);
	       }
	       if (tmp_p1) *tmp_p1='\1';
	    }
	    if (! temp_al->ma.a.entry[show1] && temp_al->ma.a.entry[show2]) {
	       if ((tmp_p2 = strchr(temp_al->ma.a.entry[show2],'\1'))) *tmp_p2='\0';
	       multibyte_safe_strncpy(str, temp_al->ma.a.entry[show2], ADDRESS_MAX_CLIST_NAME);
	       if (tmp_p2) *tmp_p2='\1';
	    }
	 } else if (temp_al->ma.a.entry[show3]) {
	    if ((tmp_p3 = strchr(temp_al->ma.a.entry[show3],'\1'))) *tmp_p3='\0';
	    multibyte_safe_strncpy(str, temp_al->ma.a.entry[show3], ADDRESS_MAX_CLIST_NAME);
	    if (tmp_p3) *tmp_p3='\1';
	 } else {
	    strcpy(str, _("-Unnamed-"));
	 }
	 gtk_clist_append(GTK_CLIST(clist), empty_line);
      } else {
	 str[0]='\0';
	 field1=field2=field3=blank;
	 tmp_delim1=delim1;
	 tmp_delim2=delim2;
	 if (temp_al->ma.a.entry[show1]) field1=temp_al->ma.a.entry[show1];
	 if (temp_al->ma.a.entry[show2]) field2=temp_al->ma.a.entry[show2];
	 if (temp_al->ma.a.entry[show3]) field3=temp_al->ma.a.entry[show3];
	 if (by_company) {
	    /* Company / Last, First */
	    if (!(field1[0])) tmp_delim1=blank;
	    if ((!field2[0]) || (!field3[0])) tmp_delim2=blank;
	    if ((!field2[0]) && (!field3[0])) tmp_delim1=blank;
	 } else {
	    /* Last, First / Company */
	    if ((!field1[0]) || (!field2[0])) tmp_delim1=blank;
	    if (!(field3[0])) tmp_delim2=blank;
	    if ((!field1[0]) && (!field2[0])) tmp_delim2=blank;
	 }
	 g_snprintf(str, ADDRESS_MAX_CLIST_NAME, "%s%s%s%s%s",
		    field1, tmp_delim1, field2, tmp_delim2, field3);
	 if (strlen(str)<1) strcpy(str, _("-Unnamed-"));
	 str[ADDRESS_MAX_CLIST_NAME]='\0';

	 gtk_clist_append(GTK_CLIST(clist), empty_line);
      }

      lstrncpy_remove_cr_lfs(str2, str, ADDRESS_MAX_COLUMN_LEN);
      gtk_clist_set_text(GTK_CLIST(clist), entries_shown, ADDRESS_NAME_COLUMN, str2);
      /* Clear string so previous data won't be used inadvertently in next set_text */
      str2[0] = '\0';
      lstrncpy_remove_cr_lfs(str2, temp_al->ma.a.entry[temp_al->ma.a.showPhone+3], ADDRESS_MAX_COLUMN_LEN);
      gtk_clist_set_text(GTK_CLIST(clist), entries_shown, ADDRESS_PHONE_COLUMN, str2);
      gtk_clist_set_row_data(GTK_CLIST(clist), entries_shown, &(temp_al->ma));

      /* Highlight row background depending on status */
      switch (temp_al->ma.rt) {
       case NEW_PC_REC:
       case REPLACEMENT_PALM_REC:
	 set_bg_rgb_clist_row(clist, entries_shown,
			  CLIST_NEW_RED, CLIST_NEW_GREEN, CLIST_NEW_BLUE);
	 break;
       case DELETED_PALM_REC:
       case DELETED_PC_REC:
	 set_bg_rgb_clist_row(clist, entries_shown,
			  CLIST_DEL_RED, CLIST_DEL_GREEN, CLIST_DEL_BLUE);
	 break;
       case MODIFIED_PALM_REC:
	 set_bg_rgb_clist_row(clist, entries_shown,
			  CLIST_MOD_RED, CLIST_MOD_GREEN, CLIST_MOD_BLUE);
	 break;
       default:
	 if (temp_al->ma.attrib & dlpRecAttrSecret) {
	    set_bg_rgb_clist_row(clist, entries_shown, 
			     CLIST_PRIVATE_RED, CLIST_PRIVATE_GREEN, CLIST_PRIVATE_BLUE);
	 } else {
	    gtk_clist_set_row_style(GTK_CLIST(clist), entries_shown, NULL);
	 }
      }

      /* Put a note pixmap up */
      if (temp_al->ma.a.entry[18]) {
	 gtk_clist_set_pixmap(GTK_CLIST(clist), entries_shown, ADDRESS_NOTE_COLUMN, pixmap_note, mask_note);
      } else {
	 gtk_clist_set_text(GTK_CLIST(clist), entries_shown, ADDRESS_NOTE_COLUMN, "");
      }

      entries_shown++;
   }

   /* If there are items in the list, highlight the selected row */
   if ((main) && (entries_shown>0)) {
      /* Select the existing requested row, or row 0 if that is impossible */
      if (clist_row_selected <= entries_shown)
      {
	 gtk_clist_select_row(GTK_CLIST(clist), clist_row_selected, ADDRESS_PHONE_COLUMN);
      }
      else
      {
	 gtk_clist_select_row(GTK_CLIST(clist), 0, ADDRESS_PHONE_COLUMN);
      }
   }

   /* Unfreeze clist after all changes */
   gtk_clist_thaw(GTK_CLIST(clist));

   if (tooltip_widget) {
      if (addr_list==NULL) {
	 gtk_tooltips_set_tip(glob_tooltips, category_menu1, _("0 records"), NULL);
      }
      else {
	 sprintf(str, _("%d of %d records"), entries_shown, num_entries);
	 gtk_tooltips_set_tip(glob_tooltips, category_menu1, str, NULL);
      }
   }

}

/*default set is which menu item is to be set on by default */
/*set is which set in the menu_item array to use */
static int make_phone_menu(int default_set, unsigned int callback_id, int set)
{
   int i;
   GSList *group;

   phone_list_menu[set] = gtk_option_menu_new();

   menu = gtk_menu_new();
   group = NULL;

   for (i=0; i<8; i++) {
      if (address_app_info.phoneLabels[i][0]) {
	 menu_item[set][i] = gtk_radio_menu_item_new_with_label(
			group, address_app_info.phoneLabels[i]);
	 gtk_signal_connect(GTK_OBJECT(menu_item[set][i]), "activate",
			    GTK_SIGNAL_FUNC(cb_phone_menu),
			    GINT_TO_POINTER(callback_id + i));
	 group = gtk_radio_menu_item_group(GTK_RADIO_MENU_ITEM(menu_item[set][i]));
	 gtk_menu_append(GTK_MENU(menu), menu_item[set][i]);
	 gtk_widget_show(menu_item[set][i]);
      }
   }
   /*Set this one to active */
   if (GTK_IS_WIDGET(menu_item[set][default_set])) {
      gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(
				     menu_item[set][default_set]), TRUE);
   }

   gtk_option_menu_set_menu(GTK_OPTION_MENU(phone_list_menu[set]), menu);
   /*Make this one show up by default */
   gtk_option_menu_set_history(GTK_OPTION_MENU(phone_list_menu[set]),
			       default_set);

   gtk_widget_show(phone_list_menu[set]);

   return 0;
}

/* returns 1 if found, 0 if not */
static int address_find()
{
   int r, found_at, total_count;

   r = 0;
   if (glob_find_id) {
      r = clist_find_id(clist,
			glob_find_id,
			&found_at,
			&total_count);
      if (r) {
	 /*avoid dividing by zero */
	 if (total_count == 0) {
	    total_count = 1;
	 }
	 gtk_clist_select_row(GTK_CLIST(clist), found_at, ADDRESS_PHONE_COLUMN);
	 if (!gtk_clist_row_is_visible(GTK_CLIST(clist), found_at)) {
	    gtk_clist_moveto(GTK_CLIST(clist), found_at, 0, 0.5, 0.0);
	 }
      }
      glob_find_id = 0;
   }
   return r;
}

/* This redraws the clist */
int address_clist_redraw()
{
   address_update_clist(clist, category_menu1, &glob_address_list,
			address_category, TRUE);

   return 0;
}

int address_cycle_cat()
{
   int b;
   int i, new_cat;

   b=dialog_save_changed_record(pane, record_changed);
   if (b==DIALOG_SAID_1) {
      cb_add_new_record(NULL, GINT_TO_POINTER(record_changed));
   }

   if (address_category == CATEGORY_ALL) {
      new_cat = -1;
   } else {
      new_cat = find_sorted_cat(address_category);
   }
   for (i=0; i<NUM_ADDRESS_CAT_ITEMS; i++) {
      new_cat++;
      if (new_cat >= NUM_ADDRESS_CAT_ITEMS) {
	 address_category = CATEGORY_ALL;
	 break;
      }
      if ((sort_l[new_cat].Pcat) && (sort_l[new_cat].Pcat[0])) {
	 address_category = sort_l[new_cat].cat_num;
	 break;
      }
   }
   clist_row_selected = 0;

   return 0;
}

int address_refresh()
{
   int index;

   if (glob_find_id) {
      address_category = CATEGORY_ALL;
   }
   if (address_category==CATEGORY_ALL) {
      index=0;
   } else {
      index=find_sorted_cat(address_category)+1;
   }
   address_update_clist(clist, category_menu1, &glob_address_list,
			address_category, TRUE);
   if (index<0) {
      jp_logf(JP_LOG_WARN, _("Category is not legal\n"));
   } else {
      gtk_option_menu_set_history(GTK_OPTION_MENU(category_menu1), index);
      gtk_check_menu_item_set_active
	(GTK_CHECK_MENU_ITEM(address_cat_menu_item1[index]), TRUE);
   }
   address_find();
   return 0;
}


static gboolean
cb_key_pressed_quickfind(GtkWidget *widget, GdkEventKey *event, gpointer data)
{
   int row_count;
   int select_row;
   int add;

   add=0;
   if ((event->keyval == GDK_KP_Down) || (event->keyval == GDK_Down)) {
      add=1;
   }
   if ((event->keyval == GDK_KP_Up) || (event->keyval == GDK_Up)) {
      add=-1;
   }
   if (!add) return FALSE;
   row_count=((GtkCList *)clist)->rows;
   if (!row_count) return FALSE;

   gtk_signal_emit_stop_by_name(GTK_OBJECT(widget), "key_press_event"); 

   select_row=clist_row_selected+add;
   if (select_row>row_count-1) {
      select_row=0;
   }
   if (select_row<0) {
      select_row=row_count-1;
   }
   gtk_clist_select_row(GTK_CLIST(clist), select_row, ADDRESS_NAME_COLUMN);
   if (!gtk_clist_row_is_visible(GTK_CLIST(clist), select_row)) {
      gtk_clist_moveto(GTK_CLIST(clist), select_row, 0, 0.5, 0.0);
   }
   return TRUE;
}

static gboolean
  cb_key_pressed(GtkWidget *widget, GdkEventKey *event,
		 gpointer next_widget) 
{
   /* This is needed because the text boxes aren't shown on the 
    * screen in the same order as the array.  I show them in the
    * same order that the palm does */
   int order[NUM_ADDRESS_ENTRIES]={
      0,1,13,2,3,4,5,6,7,8,9,10,11,12,14,15,16,17,18
   };
   int kana_order[NUM_ADDRESS_ENTRIES+NUM_ADDRESS_EXT_ENTRIES]={
      19,20,0,1,13,21,2,3,4,5,6,7,8,9,10,11,12,14,15,16,17,18
   };
   int page[NUM_ADDRESS_ENTRIES]={
      0,0,0,0,0,0,0,0,0,1,1,1,1,1,2,2,2,2,2
   };
   int kana_page[NUM_ADDRESS_ENTRIES + NUM_ADDRESS_EXT_ENTRIES]={
      0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,2,2,2,2,2
   };
   int i;
   long use_jos, char_set;
#ifdef ENABLE_GTK2
   GtkTextIter    cursor_pos_iter;
   GtkTextBuffer *text_buffer;
#endif

   if ((event->keyval == GDK_Tab) ||
       (event->keyval == GDK_ISO_Left_Tab)) {
      /* See if they are at the end of the text */
#ifdef ENABLE_GTK2
      text_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(widget));
      gtk_text_buffer_get_iter_at_mark(text_buffer,&cursor_pos_iter,gtk_text_buffer_get_insert(text_buffer));
      if (gtk_text_iter_is_end(&cursor_pos_iter)) {
#else
      if (gtk_text_get_point(GTK_TEXT(widget)) ==
	  gtk_text_get_length(GTK_TEXT(widget))) {
#endif
	 gtk_signal_emit_stop_by_name(GTK_OBJECT(widget), "key_press_event"); 
	 /* Find the next/prev widget */
	 get_pref(PREF_CHAR_SET, &char_set, NULL);
	 get_pref(PREF_USE_JOS, &use_jos, NULL);
	 if (!use_jos && (char_set == CHAR_SET_JAPANESE)) {
	    for (i=0; i<(NUM_ADDRESS_ENTRIES+NUM_ADDRESS_EXT_ENTRIES); i++) {
	       if (address_text[kana_order[i]] == widget) {
		  break;
	       }
	    }
	    if (event->keyval == GDK_Tab)  i++;
	    if (event->keyval == GDK_ISO_Left_Tab)  i--;
	    if (i>=NUM_ADDRESS_ENTRIES+NUM_ADDRESS_EXT_ENTRIES)  i=0;
	    if (i<0)  i=NUM_ADDRESS_ENTRIES+NUM_ADDRESS_EXT_ENTRIES-1;
	 } else {
	    for (i=0; i<NUM_ADDRESS_ENTRIES; i++) {
	       if (address_text[order[i]] == widget) {
		  break;
	       }
	    }
	    if (event->keyval == GDK_Tab)  i++;
	    if (event->keyval == GDK_ISO_Left_Tab)  i--;
	    if (i>=NUM_ADDRESS_ENTRIES)  i=0;
	    if (i<0)  i=NUM_ADDRESS_ENTRIES-1;
	 }
	 if (!use_jos && (char_set == CHAR_SET_JAPANESE)) {
	    gtk_notebook_set_page(GTK_NOTEBOOK(notebook), kana_page[i]);
	    gtk_widget_grab_focus(GTK_WIDGET(address_text[kana_order[i]]));
	 } else {
	    gtk_notebook_set_page(GTK_NOTEBOOK(notebook), page[i]);
	    gtk_widget_grab_focus(GTK_WIDGET(address_text[order[i]]));
	 }
	 return TRUE;
      }
   }
   return FALSE;
}

int address_gui_cleanup()
{
   int b;

   b=dialog_save_changed_record(pane, record_changed);
   if (b==DIALOG_SAID_1) {
      cb_add_new_record(NULL, GINT_TO_POINTER(record_changed));
   }
   free_AddressList(&glob_address_list);
   connect_changed_signals(DISCONNECT_SIGNALS);
#ifdef ENABLE_GTK2
   set_pref(PREF_ADDRESS_PANE, gtk_paned_get_position(GTK_PANED(pane)), NULL, TRUE);
#else
   set_pref(PREF_ADDRESS_PANE, GTK_PANED(pane)->handle_xpos, NULL, TRUE);
#endif
   set_pref(PREF_LAST_ADDR_CATEGORY, address_category, NULL, TRUE);

   return 0;
}

/*
 * Main function
 */
int address_gui(GtkWidget *vbox, GtkWidget *hbox)
{
   extern GtkWidget *glob_date_label;
   extern int glob_date_timer_tag;
   GtkWidget *scrolled_window;
   GtkWidget *pixmapwid;
   GdkPixmap *pixmap;
   GdkBitmap *mask;
   GtkWidget *vbox1, *vbox2;
   GtkWidget *hbox_temp;
   GtkWidget *vbox_temp1, *vbox_temp2, *vbox_temp3, *hbox_temp4;
   GtkWidget *separator;
   GtkWidget *label;
   GtkWidget *button;
   GtkWidget *frame;
   GtkWidget *table1, *table2, *table3;
   GtkWidget *notebook_tab;
   GSList *group;
   long ivalue, notebook_page;
   const char *svalue;
   char *titles[]={"","",""};
   GtkAccelGroup *accel_group;

   int i, i1, i2;
   int order[NUM_ADDRESS_ENTRIES+1]={
      0,1,13,2,3,4,5,6,7,8,9,10,11,12,14,15,16,17,18,0
   };
   int kana_order[NUM_ADDRESS_ENTRIES+NUM_ADDRESS_EXT_ENTRIES+1]={
      19,20,0,1,13,21,2,3,4,5,6,7,8,9,10,11,12,14,15,16,17,18,0
   };
   long use_jos, char_set;

   clist_row_selected=0;

   init();

   get_address_app_info(&address_app_info);

   for (i=0; i<NUM_ADDRESS_CAT_ITEMS; i++) {
      sort_l[i].Pcat=address_app_info.category.name[i];
      sort_l[i].cat_num=i;
   }
   qsort(sort_l, NUM_ADDRESS_CAT_ITEMS, sizeof(struct sorted_cats), cat_compare);
#ifdef JPILOT_DEBUG
   for (i=0; i<NUM_ADDRESS_CAT_ITEMS; i++) {
      printf("cat %d [%s]\n", sort_l[i].cat_num, sort_l[i].Pcat);
   }
#endif

   get_pref(PREF_LAST_ADDR_CATEGORY, &ivalue, NULL);
   address_category = ivalue;

   if (address_app_info.category.name[address_category][0]=='\0') {
      address_category=CATEGORY_ALL;
   }

   accel_group = gtk_accel_group_new();
   gtk_window_add_accel_group(GTK_WINDOW(gtk_widget_get_toplevel(vbox)),
      accel_group);

   pane = gtk_hpaned_new();
   get_pref(PREF_ADDRESS_PANE, &ivalue, &svalue);
   gtk_paned_set_position(GTK_PANED(pane), ivalue + 2);

   gtk_box_pack_start(GTK_BOX(hbox), pane, TRUE, TRUE, 5);

   vbox1 = gtk_vbox_new(FALSE, 0);
   vbox2 = gtk_vbox_new(FALSE, 0);
   gtk_paned_pack1(GTK_PANED(pane), vbox1, TRUE, FALSE);
   gtk_paned_pack2(GTK_PANED(pane), vbox2, TRUE, FALSE);

   /* Separator */
   separator = gtk_hseparator_new();
   gtk_box_pack_start(GTK_BOX(vbox1), separator, FALSE, FALSE, 5);

   /* Make the Today is: label */
   glob_date_label = gtk_label_new(" ");
   gtk_box_pack_start(GTK_BOX(vbox1), glob_date_label, FALSE, FALSE, 0);
   timeout_date(NULL);
   glob_date_timer_tag = gtk_timeout_add(CLOCK_TICK, timeout_date, NULL);

   /* Separator */
   separator = gtk_hseparator_new();
   gtk_box_pack_start(GTK_BOX(vbox1), separator, FALSE, FALSE, 5);

   /* Category Box */
   hbox_temp = gtk_hbox_new(FALSE, 0);
   gtk_box_pack_start(GTK_BOX(vbox1), hbox_temp, FALSE, FALSE, 0);

   /* Put the category menu up */
   make_category_menu(&category_menu1, address_cat_menu_item1,
		      sort_l, cb_category, TRUE);
   gtk_box_pack_start(GTK_BOX(hbox_temp), category_menu1, TRUE, TRUE, 0);

   /* Edit category button */
   button = gtk_button_new_with_label(_("Edit Categories"));
   gtk_signal_connect(GTK_OBJECT(button), "clicked",
		      GTK_SIGNAL_FUNC(cb_edit_cats), NULL);
   gtk_box_pack_start(GTK_BOX(hbox_temp), button, FALSE, FALSE, 0);

   
   /* Put the address list window up */
   scrolled_window = gtk_scrolled_window_new(NULL, NULL);
   /*gtk_widget_set_usize(GTK_WIDGET(scrolled_window), 150, 0); */
   gtk_container_set_border_width(GTK_CONTAINER(scrolled_window), 0);
   gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window),
				  GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
   gtk_box_pack_start(GTK_BOX(vbox1), scrolled_window, TRUE, TRUE, 0);

   clist = gtk_clist_new_with_titles(3, titles);
   clist_hack=FALSE;
   gtk_clist_column_title_passive(GTK_CLIST(clist), ADDRESS_PHONE_COLUMN);
   gtk_clist_column_title_passive(GTK_CLIST(clist), ADDRESS_NOTE_COLUMN);

   if (address_app_info.sortByCompany) {
      gtk_clist_set_column_title(GTK_CLIST(clist), ADDRESS_NAME_COLUMN, _("Company/Name"));
   } else {
      gtk_clist_set_column_title(GTK_CLIST(clist), ADDRESS_NAME_COLUMN, _("Name/Company"));
   }
   gtk_signal_connect(GTK_OBJECT(GTK_CLIST(clist)->column[ADDRESS_NAME_COLUMN].button),
		      "clicked", GTK_SIGNAL_FUNC(cb_resort), 
		      GINT_TO_POINTER(address_app_info.sortByCompany));
   gtk_clist_set_column_title(GTK_CLIST(clist), ADDRESS_PHONE_COLUMN, _("Phone"));
   /* Put pretty pictures in the clist column headings */
   get_pixmaps(vbox, PIXMAP_NOTE, &pixmap, &mask);
   pixmapwid = gtk_pixmap_new(pixmap, mask);
   hack_clist_set_column_title_pixmap(clist, ADDRESS_NOTE_COLUMN, pixmapwid);

   gtk_signal_connect(GTK_OBJECT(clist), "select_row",
		      GTK_SIGNAL_FUNC(cb_clist_selection),
		      text);
   gtk_clist_set_shadow_type(GTK_CLIST(clist), SHADOW);
   gtk_clist_set_selection_mode(GTK_CLIST(clist), GTK_SELECTION_BROWSE);

   gtk_clist_set_column_auto_resize(GTK_CLIST(clist), ADDRESS_NAME_COLUMN, TRUE);
   gtk_clist_set_column_auto_resize(GTK_CLIST(clist), ADDRESS_NOTE_COLUMN, TRUE);
   gtk_clist_set_column_auto_resize(GTK_CLIST(clist), ADDRESS_PHONE_COLUMN, FALSE);

   gtk_container_add(GTK_CONTAINER(scrolled_window), GTK_WIDGET(clist));
   /*gtk_clist_set_column_justification(GTK_CLIST(clist), 1, GTK_JUSTIFY_RIGHT); */

   hbox_temp = gtk_hbox_new(FALSE, 0);
   gtk_box_pack_start(GTK_BOX(vbox1), hbox_temp, FALSE, FALSE, 0);

   label = gtk_label_new(_("Quick Find"));
   gtk_box_pack_start(GTK_BOX(hbox_temp), label, FALSE, FALSE, 0);

   address_quickfind_entry = gtk_entry_new();
   gtk_signal_connect(GTK_OBJECT(address_quickfind_entry), "key_press_event",
		      GTK_SIGNAL_FUNC(cb_key_pressed_quickfind), NULL);
   gtk_signal_connect(GTK_OBJECT(address_quickfind_entry), "changed",
		      GTK_SIGNAL_FUNC(cb_address_quickfind),
		      NULL);
   gtk_box_pack_start(GTK_BOX(hbox_temp), address_quickfind_entry, TRUE, TRUE, 0);
   gtk_widget_grab_focus(GTK_WIDGET(address_quickfind_entry));

   /* The new entry gui */
   hbox_temp = gtk_hbox_new(FALSE, 0);
   gtk_box_pack_start(GTK_BOX(vbox2), hbox_temp, FALSE, FALSE, 0);

   /* Delete Button */
   delete_record_button = gtk_button_new_with_label(_("Delete"));
   gtk_signal_connect(GTK_OBJECT(delete_record_button), "clicked",
		      GTK_SIGNAL_FUNC(cb_delete_address),
		      GINT_TO_POINTER(DELETE_FLAG));
   gtk_box_pack_start(GTK_BOX(hbox_temp), delete_record_button, TRUE, TRUE, 0);
   gtk_widget_add_accelerator(delete_record_button, "clicked", accel_group,
	 GDK_d, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
   gtk_tooltips_set_tip(glob_tooltips, delete_record_button, _("Delete the selected record   Ctrl+D"), NULL);

   undelete_record_button = gtk_button_new_with_label(_("Undelete"));
   gtk_signal_connect(GTK_OBJECT(undelete_record_button), "clicked",
		      GTK_SIGNAL_FUNC(cb_undelete_address),
		      GINT_TO_POINTER(UNDELETE_FLAG));
   gtk_box_pack_start(GTK_BOX(hbox_temp), undelete_record_button, TRUE, TRUE, 0);

   /* Create "Copy" button */
   copy_record_button = gtk_button_new_with_label(_("Copy"));
   gtk_signal_connect(GTK_OBJECT(copy_record_button), "clicked",
		      GTK_SIGNAL_FUNC(cb_add_new_record),
		      GINT_TO_POINTER(COPY_FLAG));
   gtk_box_pack_start(GTK_BOX(hbox_temp), copy_record_button, TRUE, TRUE, 0);
   gtk_widget_add_accelerator(copy_record_button, "clicked", accel_group, GDK_o,
      GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
   gtk_tooltips_set_tip(glob_tooltips, copy_record_button, _("Copy the record   Ctrl+O"), NULL);

   /* Create "New" button */
   new_record_button = gtk_button_new_with_label(_("New Record"));
   gtk_signal_connect(GTK_OBJECT(new_record_button), "clicked",
		      GTK_SIGNAL_FUNC(cb_address_clear), NULL);
   gtk_box_pack_start(GTK_BOX(hbox_temp), new_record_button, TRUE, TRUE, 0);
   gtk_widget_add_accelerator(new_record_button, "clicked", accel_group, GDK_n,
      GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
   gtk_tooltips_set_tip(glob_tooltips, new_record_button, _("Add a new record   Ctrl+N"), NULL);

   /* Create "Add Record" button */
   add_record_button = gtk_button_new_with_label(_("Add Record"));
   gtk_signal_connect(GTK_OBJECT(add_record_button), "clicked",
		      GTK_SIGNAL_FUNC(cb_add_new_record),
		      GINT_TO_POINTER(NEW_FLAG));
   gtk_box_pack_start(GTK_BOX(hbox_temp), add_record_button, TRUE, TRUE, 0);
   gtk_widget_set_name(GTK_WIDGET(GTK_LABEL(GTK_BIN(add_record_button)->child)),
		       "label_high");
   gtk_widget_add_accelerator(add_record_button, "clicked", accel_group,
      GDK_r, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
   gtk_tooltips_set_tip(glob_tooltips, add_record_button, _("Add the new record   Ctrl+R"), NULL);

   /* Create "apply changes" button */
   apply_record_button = gtk_button_new_with_label(_("Apply Changes"));
   gtk_signal_connect(GTK_OBJECT(apply_record_button), "clicked",
		      GTK_SIGNAL_FUNC(cb_add_new_record),
		      GINT_TO_POINTER(MODIFY_FLAG));
   gtk_box_pack_start(GTK_BOX(hbox_temp), apply_record_button, TRUE, TRUE, 0);
   gtk_widget_set_name(GTK_WIDGET(GTK_LABEL(GTK_BIN(apply_record_button)->child)),
		       "label_high");
   gtk_widget_add_accelerator(apply_record_button, "clicked", accel_group,
      GDK_Return, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
   gtk_tooltips_set_tip(glob_tooltips, apply_record_button, _("Commit the modifications   Ctrl+Enter"), NULL);

   /*Separator */
   separator = gtk_hseparator_new();
   gtk_box_pack_start(GTK_BOX(vbox2), separator, FALSE, FALSE, 5);


   /*Private check box */
   hbox_temp = gtk_hbox_new(FALSE, 0);
   gtk_box_pack_start(GTK_BOX(vbox2), hbox_temp, FALSE, FALSE, 0);
   private_checkbox = gtk_check_button_new_with_label(_("Private"));
   gtk_box_pack_end(GTK_BOX(hbox_temp), private_checkbox, FALSE, FALSE, 0);

   /*Add the new category menu */
   make_category_menu(&category_menu2, address_cat_menu_item2,
		      sort_l, NULL, FALSE);

   gtk_box_pack_start(GTK_BOX(hbox_temp), category_menu2, TRUE, TRUE, 0);


   /*Add the notebook for new entries */
   notebook = gtk_notebook_new();
   gtk_notebook_set_tab_pos(GTK_NOTEBOOK(notebook), GTK_POS_TOP);
   gtk_notebook_popup_enable(GTK_NOTEBOOK(notebook));
   gtk_signal_connect(GTK_OBJECT(notebook), "switch-page",
		      GTK_SIGNAL_FUNC(cb_notebook_changed), NULL);

   gtk_box_pack_start(GTK_BOX(vbox2), notebook, TRUE, TRUE, 0);

   /*Page 1 */
   notebook_tab = gtk_label_new(_("Name"));
   vbox_temp1 = gtk_vbox_new(FALSE, 0);
   gtk_notebook_append_page(GTK_NOTEBOOK(notebook), vbox_temp1, notebook_tab);
   /* Notebook tabs have to be shown before the show_all */
   gtk_widget_show(vbox_temp1);
   gtk_widget_show(notebook_tab);

   /*Page 2 */
   notebook_tab = gtk_label_new(_("Address"));
   vbox_temp2 = gtk_vbox_new(FALSE, 0);
   gtk_notebook_append_page(GTK_NOTEBOOK(notebook), vbox_temp2, notebook_tab);
   /* Notebook tabs have to be shown before the show_all */
   gtk_widget_show(vbox_temp2);
   gtk_widget_show(notebook_tab);

   /*Page 3 */
   notebook_tab = gtk_label_new(_("Other"));
   vbox_temp3 = gtk_vbox_new(FALSE, 0);
   gtk_notebook_append_page(GTK_NOTEBOOK(notebook), vbox_temp3, notebook_tab);
   /* Notebook tabs have to be shown before the show_all */
   gtk_widget_show(vbox_temp3);
   gtk_widget_show(notebook_tab);

   /*Page 4 */
   notebook_tab = gtk_label_new(_("All"));
   hbox_temp4 = gtk_vbox_new(FALSE, 0);
   gtk_notebook_append_page(GTK_NOTEBOOK(notebook), hbox_temp4, notebook_tab);
   /* Notebook tabs have to be shown before the show_all */
   gtk_widget_show(hbox_temp4);
   gtk_widget_show(notebook_tab);

   /*Put a table on every page */
   table1 = gtk_table_new(9, 4, FALSE);
   gtk_box_pack_start(GTK_BOX(vbox_temp1), table1, TRUE, TRUE, 0);
   table2 = gtk_table_new(9, 2, FALSE);
   gtk_box_pack_start(GTK_BOX(vbox_temp2), table2, TRUE, TRUE, 0);
   table3 = gtk_table_new(9, 2, FALSE);
   gtk_box_pack_start(GTK_BOX(vbox_temp3), table3, TRUE, TRUE, 0);

   get_pref(PREF_CHAR_SET, &char_set, NULL);
   get_pref(PREF_USE_JOS, &use_jos, NULL);
   if (!use_jos && (char_set == CHAR_SET_JAPANESE)) {
      label = NULL;
      for (i=0; i<(NUM_ADDRESS_ENTRIES+NUM_ADDRESS_EXT_ENTRIES); i++) {
	 i2=kana_order[i];
	 if (i2>2 && i2<8) {
	    make_phone_menu(i2-3, (i2-3)<<4, i2-3);
	 } else if (i2 < NUM_ADDRESS_ENTRIES) {
	    label = gtk_label_new(address_app_info.labels[i2]); 
	    /*gtk_label_set_justify(GTK_LABEL(label), GTK_JUSTIFY_LEFT); */
	    gtk_misc_set_alignment(GTK_MISC(label), 1.0, 0.5);
	 } else {
	    char tmp_buf[64];
	    strcpy(tmp_buf, _("kana("));
	    strcat(tmp_buf, address_app_info.labels[i2-NUM_ADDRESS_ENTRIES]);
	    strcat(tmp_buf, ")");
	    label = gtk_label_new(tmp_buf); 
	    gtk_misc_set_alignment(GTK_MISC(label), 1.0, 0.5);
	 }
#ifdef ENABLE_GTK2
	 address_text[i2] = gtk_text_view_new();
	 gtk_txt_buf_address_text[i2] = G_OBJECT(gtk_text_view_get_buffer(GTK_TEXT_VIEW(address_text[i2])));
	 gtk_text_view_set_editable(GTK_TEXT_VIEW(address_text[i2]), TRUE);
	 gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(address_text[i2]), GTK_WRAP_WORD);
	 gtk_container_set_border_width(GTK_CONTAINER(address_text[i2]), 1);
#else
	 address_text[i2] = gtk_text_new(NULL, NULL);

	 gtk_text_set_editable(GTK_TEXT(address_text[i2]), TRUE);
	 gtk_text_set_word_wrap(GTK_TEXT(address_text[i2]), TRUE);
#endif
	 gtk_widget_set_usize(GTK_WIDGET(address_text[i2]), 0, 25);
	 /*gtk_box_pack_start(GTK_BOX(hbox_temp), address_text[i2], TRUE, TRUE, 0); */
	 /*hbox_temp = gtk_hbox_new(FALSE, 0); */
	 if (i<(9+3)) {
	    if (i2>2 && i2<8) {
	       gtk_table_attach(GTK_TABLE(table1), GTK_WIDGET(phone_list_menu[i2-3]),
				2, 3, i, i+1, GTK_SHRINK, 0, 0, 0);
	    } else {
	       gtk_table_attach(GTK_TABLE(table1), GTK_WIDGET(label),
				2, 3, i, i+1, GTK_FILL, 0, 0, 0);
	    }
	    gtk_table_attach_defaults(GTK_TABLE(table1), GTK_WIDGET(address_text[i2]),
				      3, 4, i, i+1);
	 }
	 if (i>(8+3) && i<(14+3)) {
#ifdef ENABLE_GTK2
	    gtk_table_attach(GTK_TABLE(table2), GTK_WIDGET(label),
				      0, 1, i-9-3, i-8-3,GTK_FILL, 0, 2, 0);
#else
	    gtk_table_attach_defaults(GTK_TABLE(table2), GTK_WIDGET(label),
				      0, 1, i-9-3, i-8-3);
#endif
	    gtk_table_attach_defaults(GTK_TABLE(table2), GTK_WIDGET(address_text[i2]),
				      1, 2, i-9-3, i-8-3);
 	   }
	 if (i>(13+3) && i<100) {
#ifdef ENABLE_GTK2
	    gtk_table_attach(GTK_TABLE(table3), GTK_WIDGET(label),
				      0, 1, i-14-3, i-13-3,GTK_FILL, 0, 2, 0);
#else
	    gtk_table_attach_defaults(GTK_TABLE(table3), GTK_WIDGET(label),
				      0, 1, i-14-3, i-13-3);
#endif
	    gtk_table_attach_defaults(GTK_TABLE(table3), GTK_WIDGET(address_text[i2]),
				      1, 2, i-14-3, i-13-3);
	 }
      }

      /* Capture the TAB key to change focus with it */
      for (i=0; i<(NUM_ADDRESS_ENTRIES+NUM_ADDRESS_EXT_ENTRIES); i++) {
	 i1=kana_order[i];
	 i2=kana_order[i+1];
	 if (i2<(NUM_ADDRESS_ENTRIES+NUM_ADDRESS_EXT_ENTRIES)) {
	    gtk_signal_connect(GTK_OBJECT(address_text[i1]), "key_press_event",
			       GTK_SIGNAL_FUNC(cb_key_pressed), address_text[i2]);
	 }
      }

      /* Put some radio buttons for selecting which number to display in view */
      group = NULL;
      for (i=0; i<NUM_PHONE_ENTRIES; i++) {
	 radio_button[i] = gtk_radio_button_new(group);
	 group = gtk_radio_button_group(GTK_RADIO_BUTTON(radio_button[i]));
	 gtk_table_attach(GTK_TABLE(table1), GTK_WIDGET(radio_button[i]),
			  1, 2, i+4+3, i+5+3, GTK_SHRINK, 0, 0, 0);
      }
   } else {
      label = NULL;
      for (i=0; i<NUM_ADDRESS_ENTRIES; i++) {
	 i2=order[i];
	 if (i2>2 && i2<8) {
	    make_phone_menu(i2-3, (i2-3)<<4, i2-3);
	 } else {
	    label = gtk_label_new(address_app_info.labels[i2]); 
	    gtk_misc_set_alignment(GTK_MISC(label), 1.0, 0.5);
	 }
#ifdef ENABLE_GTK2
	 address_text[i2] = gtk_text_view_new();
	 gtk_txt_buf_address_text[i2] = G_OBJECT(gtk_text_view_get_buffer(GTK_TEXT_VIEW(address_text[i2])));
	 gtk_text_view_set_editable(GTK_TEXT_VIEW(address_text[i2]), TRUE);
	 gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(address_text[i2]), GTK_WRAP_WORD);
	 gtk_container_set_border_width(GTK_CONTAINER(address_text[i2]), 1);
#else
	 address_text[i2] = gtk_text_new(NULL, NULL);

	 gtk_text_set_editable(GTK_TEXT(address_text[i2]), TRUE);
	 gtk_text_set_word_wrap(GTK_TEXT(address_text[i2]), TRUE);
#endif
	 gtk_widget_set_usize(GTK_WIDGET(address_text[i2]), 0, 25);
	 if (i<9) {
	    if (i2>2 && i2<8) {
	       gtk_table_attach(GTK_TABLE(table1), GTK_WIDGET(phone_list_menu[i2-3]),
				2, 3, i, i+1, GTK_SHRINK, 0, 0, 0);
	       if (!strcmp(address_app_info.labels[i2], _("E-mail")))
	       {
		  dial_button[i2-3] = gtk_button_new_with_label(_("Mail"));
	       }
	       else
	       {
		  dial_button[i2-3] = gtk_button_new_with_label(_("Dial"));
	       }
	       gtk_signal_connect(GTK_OBJECT(dial_button[i2-3]), "clicked",
				  GTK_SIGNAL_FUNC(cb_dial_or_mail),
			          address_text[i2]);
	       gtk_table_attach(GTK_TABLE(table1), GTK_WIDGET(dial_button[i2-3]),
				0, 1, i, i+1, GTK_SHRINK, 0, 0, 0);
	    } else {
	       gtk_table_attach(GTK_TABLE(table1), GTK_WIDGET(label),
				2, 3, i, i+1, GTK_FILL, 0, 0, 0);
	    }
	    gtk_table_attach_defaults(GTK_TABLE(table1), GTK_WIDGET(address_text[i2]), /* (mo) */
				      3, 4, i, i+1);
	 }
	 if (i>8 && i<14) {
#ifdef ENABLE_GTK2
	    gtk_table_attach(GTK_TABLE(table2), GTK_WIDGET(label),
			     0, 1, i-9, i-8,GTK_FILL, 0, 2, 0);
#else
	    gtk_table_attach_defaults(GTK_TABLE(table2), GTK_WIDGET(label),
				      0, 1, i-9, i-8);
#endif
	    gtk_table_attach_defaults(GTK_TABLE(table2), GTK_WIDGET(address_text[i2]),
				      1, 2, i-9, i-8);
	 }
	 if (i>13 && i<100) {
#ifdef ENABLE_GTK2
	    gtk_table_attach(GTK_TABLE(table3), GTK_WIDGET(label),
			     0, 1, i-14, i-13,GTK_FILL, 0, 2, 0);
#else
	    gtk_table_attach_defaults(GTK_TABLE(table3), GTK_WIDGET(label),
				      0, 1, i-14, i-13);
#endif
	    gtk_table_attach_defaults(GTK_TABLE(table3), GTK_WIDGET(address_text[i2]),
				      1, 2, i-14, i-13);
	 }
      }
  
      /* Capture the TAB key to change focus with it */
      for (i=0; i<NUM_ADDRESS_ENTRIES; i++) {
	 i1=order[i];
	 i2=order[i+1];
	 if (i2<NUM_ADDRESS_ENTRIES) {
	    gtk_signal_connect(GTK_OBJECT(address_text[i1]), "key_press_event",
			       GTK_SIGNAL_FUNC(cb_key_pressed), address_text[i2]);
	 }
      }

      /* Put some radio buttons for selecting which number to display in view */
      group = NULL;
      for (i=0; i<NUM_PHONE_ENTRIES; i++) {
	 radio_button[i] = gtk_radio_button_new_with_label(group, _("Show\nIn List"));
	 group = gtk_radio_button_group(GTK_RADIO_BUTTON(radio_button[i]));
	 gtk_table_attach(GTK_TABLE(table1), GTK_WIDGET(radio_button[i]),
			  1, 2, i+4, i+5, GTK_SHRINK, 0, 0, 0);
      }
   }

   /*The Quickview page */
   frame = gtk_frame_new(_("Quick View"));
   gtk_frame_set_label_align(GTK_FRAME(frame), 0.5, 0.0);
   gtk_box_pack_start(GTK_BOX(hbox_temp4), frame, TRUE, TRUE, 0);
   /*The text box on the right side */
   hbox_temp = gtk_hbox_new (FALSE, 0);
   gtk_container_set_border_width(GTK_CONTAINER(frame), 5);
   gtk_container_add(GTK_CONTAINER(frame), hbox_temp);

#ifdef ENABLE_GTK2
   text = gtk_text_view_new();
   gtk_txt_buf_text = G_OBJECT(gtk_text_view_get_buffer(GTK_TEXT_VIEW(text)));
   gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(text), FALSE);
   gtk_text_view_set_editable(GTK_TEXT_VIEW(text), FALSE);
   gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(text), GTK_WRAP_WORD);

   scrolled_window = gtk_scrolled_window_new (NULL, NULL);
   gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window),
				  GTK_POLICY_NEVER, GTK_POLICY_ALWAYS);
   gtk_container_set_border_width(GTK_CONTAINER(scrolled_window), 1);
   gtk_container_add(GTK_CONTAINER(scrolled_window), text);
   gtk_box_pack_start(GTK_BOX(hbox_temp), scrolled_window, TRUE, TRUE, 0);
#else
   text = gtk_text_new(NULL, NULL);
   gtk_text_set_editable(GTK_TEXT(text), FALSE);
   gtk_text_set_word_wrap(GTK_TEXT(text), TRUE);
   vscrollbar = gtk_vscrollbar_new(GTK_TEXT(text)->vadj);
   gtk_box_pack_start(GTK_BOX(hbox_temp), text, TRUE, TRUE, 0);
   gtk_box_pack_start(GTK_BOX(hbox_temp), vscrollbar, FALSE, FALSE, 0);
#endif

   gtk_widget_show_all(vbox);
   gtk_widget_show_all(hbox);

   gtk_widget_hide(add_record_button);
   gtk_widget_hide(apply_record_button);
   gtk_widget_hide(undelete_record_button);

   get_pref(PREF_ADDRESS_NOTEBOOK_PAGE, &notebook_page, NULL);

   if ((notebook_page<4) && (notebook_page>-1)) {
      gtk_notebook_set_page(GTK_NOTEBOOK(notebook), notebook_page);
   }

   address_refresh();

   return 0;
}
