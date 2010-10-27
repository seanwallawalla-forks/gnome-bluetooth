/*
 *
 *  BlueZ - Bluetooth protocol stack for Linux
 *
 *  Copyright (C) 2010 Giovanni Campagna <scampa.giovanni@gmail.com>
 *
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <dbus/dbus-glib.h>

#include <bluetooth-applet.h>
#include <lib/bluetooth-client.h>
#include <lib/bluetooth-client-private.h>
#include <lib/bluetooth-killswitch.h>
#include <lib/bluetooth-agent.h>

#include <marshal.h>

static gpointer
bluetooth_simple_device_copy (gpointer boxed)
{
	BluetoothSimpleDevice* origin = (BluetoothSimpleDevice*) boxed;

	BluetoothSimpleDevice* result = g_new (BluetoothSimpleDevice, 1);
	result->bdaddr = g_strdup (origin->bdaddr);
	result->device_path = g_strdup (origin->device_path);
	result->alias = g_strdup (origin->alias);
	result->connected = origin->connected;
	result->can_connect = origin->can_connect;
	result->capabilities = origin->capabilities;
	result->type = origin->type;

	return (gpointer)result;
}

static void
bluetooth_simple_device_free (gpointer boxed)
{
	BluetoothSimpleDevice* obj = (BluetoothSimpleDevice*) boxed;

	g_free (obj->device_path);
	g_free (obj->bdaddr);
	g_free (obj->alias);
	g_free (obj);
}

G_DEFINE_BOXED_TYPE(BluetoothSimpleDevice, bluetooth_simple_device, bluetooth_simple_device_copy, bluetooth_simple_device_free)

struct _BluetoothApplet
{
  GObject parent_instance;

  BluetoothKillswitch* killswitch_manager;
  BluetoothClient* client;
  GtkTreeModel* client_model;
  GtkTreeIter* default_adapter;
  BluetoothAgent* agent;
  GHashTable* pending_requests;

  gint num_adapters_powered;
  gint num_adapters_present;
};

struct _BluetoothAppletClass
{
  GObjectClass parent_class;

};

G_DEFINE_TYPE(BluetoothApplet, bluetooth_applet, G_TYPE_OBJECT)

enum {
	PROP_0,
	PROP_KILLSWITCH_STATE,
	PROP_DISCOVERABLE,
	PROP_FULL_MENU,
	PROP_LAST
};
static GParamSpec *properties[PROP_LAST];

enum {
	SIGNAL_DEVICES_CHANGED,

	SIGNAL_PINCODE_REQUEST,
	SIGNAL_CONFIRM_REQUEST,
	SIGNAL_AUTHORIZE_REQUEST,
	SIGNAL_CANCEL_REQUEST,

	SIGNAL_LAST
};
guint signals[SIGNAL_LAST];

/**
 * bluetooth_applet_agent_reply_passkey:
 *
 * @self: a #BluetoothApplet
 * @request_key: (transfer full): an opaque token given in the pincode-request signal
 * @passkey: (transfer full) (allow-none): the passkey entered by the user, or NULL if the dialog was dismissed
 */
void
bluetooth_applet_agent_reply_passkey(BluetoothApplet* self, gchar* request_key, gchar* passkey)
{
	DBusGMethodInvocation* context;

	g_return_if_fail (BLUETOOTH_IS_APPLET (self));
	g_return_if_fail (request_key != NULL);

	context = g_hash_table_lookup (self->pending_requests, request_key);

	if (passkey != NULL) {
		dbus_g_method_return (context, passkey);
	} else {
		GError *error;
		error = g_error_new(AGENT_ERROR, AGENT_ERROR_REJECT,
				"Pairing request rejected");
		dbus_g_method_return_error (context, error);
	}

	g_hash_table_remove (self->pending_requests, request_key);
	g_free (request_key);
	g_free (passkey);
}

/**
 * bluetooth_applet_agent_reply_pincode:
 *
 * @self: a #BluetoothApplet
 * @request_key: (transfer full): an opaque token given in the pincode-request signal
 * @pincode: the PIN code entered by the user, or -1 if the dialog was dismissed
 */
void
bluetooth_applet_agent_reply_pincode(BluetoothApplet* self, gchar* request_key, gint pincode)
{
	DBusGMethodInvocation* context;

	g_return_if_fail (BLUETOOTH_IS_APPLET (self));
	g_return_if_fail (request_key != NULL);

	context = g_hash_table_lookup (self->pending_requests, request_key);

	if (pincode != -1) {
		dbus_g_method_return (context, pincode);
	} else {
		GError *error;
		error = g_error_new(AGENT_ERROR, AGENT_ERROR_REJECT,
				"Pairing request rejected");
		dbus_g_method_return_error (context, error);
	}

	g_hash_table_remove (self->pending_requests, request_key);
	g_free (request_key);
}

/**
 * bluetooth_applet_agent_reply_confirm:
 *
 * @self: a #BluetoothApplet
 * @request_key: (transfer full): an opaque token given in the pincode-request signal
 * @confirm: TRUE if operation was confirmed, FALSE otherwise
 */
void
bluetooth_applet_agent_reply_confirm(BluetoothApplet* self, gchar* request_key, gboolean confirm)
{
	DBusGMethodInvocation* context;

	g_return_if_fail (BLUETOOTH_IS_APPLET (self));
	g_return_if_fail (request_key != NULL);

	context = g_hash_table_lookup (self->pending_requests, request_key);

	if (confirm) {
		dbus_g_method_return (context);
	} else {
		GError *error;
		error = g_error_new(AGENT_ERROR, AGENT_ERROR_REJECT,
				"Confirmation request rejected");
		dbus_g_method_return_error (context, error);
	}

	g_hash_table_remove (self->pending_requests, request_key);
	g_free (request_key);
}

/**
 * bluetooth_applet_agent_reply_auth:
 *
 * @self: a #BluetoothApplet
 * @request_key: (transfer full): an opaque token given in the pincode-request signal
 * @auth: TRUE if operation was authorized, FALSE otherwise
 * @trusted: TRUE if the operation should be authorized automatically in the future
 */
void
bluetooth_applet_agent_reply_auth(BluetoothApplet* self, gchar* request_key, gboolean auth, gboolean trusted)
{
	DBusGMethodInvocation* context;

	g_return_if_fail (BLUETOOTH_IS_APPLET (self));
	g_return_if_fail (request_key != NULL);

	context = g_hash_table_lookup (self->pending_requests, request_key);

	if (auth) {
		if (trusted)
			bluetooth_client_set_trusted (self->client, request_key, TRUE);

		dbus_g_method_return (context);
	} else {
		GError *error;
		error = g_error_new(AGENT_ERROR, AGENT_ERROR_REJECT,
				"Confirmation request rejected");
		dbus_g_method_return_error (context, error);
	}

	g_hash_table_remove (self->pending_requests, request_key);
	g_free (request_key);
}

#ifndef DBUS_TYPE_G_DICTIONARY
#define DBUS_TYPE_G_DICTIONARY \
	(dbus_g_type_get_map("GHashTable", G_TYPE_STRING, G_TYPE_VALUE))
#endif

static char *
device_get_name(DBusGProxy *proxy, char **long_name)
{
	GHashTable *hash;
	GValue *value;
	char *alias, *address;

	g_return_val_if_fail (long_name != NULL, NULL);

	if (dbus_g_proxy_call (proxy, "GetProperties",  NULL,
			       G_TYPE_INVALID,
			       DBUS_TYPE_G_DICTIONARY, &hash,
			       G_TYPE_INVALID) == FALSE) {
		return NULL;
	}

	value = g_hash_table_lookup(hash, "Address");
	if (value == NULL) {
		g_hash_table_destroy (hash);
		return NULL;
	}
	address = g_value_dup_string(value);

	value = g_hash_table_lookup(hash, "Name");
	alias = value ? g_value_dup_string(value) : address;

	g_hash_table_destroy (hash);

	if (value)
		*long_name = g_strdup_printf ("'%s' (%s)", alias, address);
	else
		*long_name = g_strdup_printf ("'%s'", address);

	if (alias != address)
		g_free (address);
	return alias;
}

static gboolean pincode_request(DBusGMethodInvocation *context,
					DBusGProxy *device, gpointer user_data)
{
	BluetoothApplet* self = BLUETOOTH_APPLET (user_data);
	char *name;
	char *long_name = NULL;
	const char *path;

	name = device_get_name (device, &long_name);
	path = dbus_g_proxy_get_path (device);
	g_hash_table_insert (self->pending_requests, g_strdup (path), g_object_ref (context));

	g_signal_emit (self, signals[SIGNAL_PINCODE_REQUEST], 0, path, name, long_name, TRUE);

	g_free (name);
	g_free (long_name);

	return TRUE;
}

static gboolean passkey_request(DBusGMethodInvocation *context,
					DBusGProxy *device, gpointer user_data)
{
	BluetoothApplet* self = BLUETOOTH_APPLET (user_data);
	char *name;
	char *long_name = NULL;
	const char *path;

	name = device_get_name (device, &long_name);
	path = dbus_g_proxy_get_path (device);
	g_hash_table_insert (self->pending_requests, g_strdup (path), g_object_ref (context));

	g_signal_emit (self, signals[SIGNAL_PINCODE_REQUEST], 0, path, name, long_name, FALSE);

	g_free (name);
	g_free (long_name);

	return TRUE;
}

static gboolean
confirm_request (DBusGMethodInvocation *context,
		 DBusGProxy *device,
		 guint pin,
		 gpointer user_data)
{
	BluetoothApplet* self = BLUETOOTH_APPLET (user_data);
	char *name;
	char *long_name = NULL;
	const char *path;

	name = device_get_name (device, &long_name);
	path = dbus_g_proxy_get_path (device);
	g_hash_table_insert (self->pending_requests, g_strdup (path), g_object_ref (context));

	g_signal_emit (self, signals[SIGNAL_CONFIRM_REQUEST], 0, path, name, long_name, pin);

	g_free (name);
	g_free (long_name);

	return TRUE;
}

static gboolean
authorize_request (DBusGMethodInvocation *context,
		   DBusGProxy *device,
		   const char *uuid,
		   gpointer user_data)
{
	BluetoothApplet* self = BLUETOOTH_APPLET (user_data);
	char *name;
	char *long_name = NULL;
	const char *path;

	name = device_get_name (device, &long_name);
	path = dbus_g_proxy_get_path (device);
	g_hash_table_insert (self->pending_requests, g_strdup (path), g_object_ref (context));

	g_signal_emit (self, signals[SIGNAL_AUTHORIZE_REQUEST], 0, path, name, long_name, uuid);

	g_free (name);
	g_free (long_name);

	return TRUE;
}

static void
cancel_request_single (gpointer key, gpointer value, gpointer user_data)
{
	DBusGMethodInvocation* request_context = value;
	GError* result;

	result = g_error_new (AGENT_ERROR, AGENT_ERROR_REJECT, "Agent callback cancelled");

	dbus_g_method_return_error (request_context, result);
}

static gboolean
cancel_request(DBusGMethodInvocation *context,
               gpointer user_data)
{
	BluetoothApplet* self = BLUETOOTH_APPLET (user_data);

	g_hash_table_foreach (self->pending_requests, cancel_request_single, NULL);
	g_hash_table_remove_all (self->pending_requests);

	g_signal_emit (self, signals[SIGNAL_CANCEL_REQUEST], 0);

	return TRUE;
}


static void
find_default_adapter (BluetoothApplet* self)
{
  GtkTreeIter iter;
  gboolean cont;

  if (self->default_adapter) {
    gtk_tree_iter_free (self->default_adapter);
    self->default_adapter = NULL;
  }
  if (self->agent) {
		bluetooth_agent_unregister (self->agent);
		g_object_unref (self->agent);
		self->agent = NULL;
  }
  self->num_adapters_present = self->num_adapters_powered = 0;

  cont = gtk_tree_model_get_iter_first (self->client_model, &iter);
  while (cont) {
    gboolean is_default, powered;

    self->num_adapters_present++;

    gtk_tree_model_get (self->client_model, &iter,
			BLUETOOTH_COLUMN_DEFAULT, &is_default,
			BLUETOOTH_COLUMN_POWERED, &powered,
			-1);
    if (powered)
      self->num_adapters_powered++;
    if (is_default && powered)
      self->default_adapter = gtk_tree_iter_copy (&iter);

    cont = gtk_tree_model_iter_next (self->client_model, &iter);
  }

  if (self->default_adapter) {
    DBusGProxy* adapter;

    gtk_tree_model_get (self->client_model, self->default_adapter,
			BLUETOOTH_COLUMN_PROXY, &adapter, -1);

    self->agent = bluetooth_agent_new();

    bluetooth_agent_set_pincode_func (self->agent, pincode_request, self);
    bluetooth_agent_set_passkey_func (self->agent, passkey_request, self);
    bluetooth_agent_set_confirm_func (self->agent, confirm_request, self);
    bluetooth_agent_set_authorize_func (self->agent, authorize_request, self);
    bluetooth_agent_set_cancel_func (self->agent, cancel_request, self);

    bluetooth_agent_register (self->agent, adapter);

    g_object_unref (adapter);
  }
}

static void
device_added_or_changed (GtkTreeModel *model,
                         GtkTreePath  *path,
                         GtkTreeIter  *iter,
                         gpointer      data)
{
	BluetoothApplet* self = BLUETOOTH_APPLET (data);

	gboolean prev_visibility = bluetooth_applet_get_discoverable (self);
	gint prev_num_adapters_powered = self->num_adapters_powered;
	gint prev_num_adapters_present = self->num_adapters_present;

	find_default_adapter (self);

	if (bluetooth_applet_get_discoverable (self) != prev_visibility)
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DISCOVERABLE]);
	if (prev_num_adapters_powered != self->num_adapters_powered ||
			prev_num_adapters_present != self->num_adapters_present) {
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_KILLSWITCH_STATE]);
		g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FULL_MENU]);
	}

	g_signal_emit (self, signals[SIGNAL_DEVICES_CHANGED], 0);
}

static void
device_removed(GtkTreeModel *model,
			   GtkTreePath *path,
			   gpointer user_data)
{
	device_added_or_changed (model, path, NULL, user_data);
}

static gboolean
set_powered_foreach (GtkTreeModel *model,
                     GtkTreePath  *path,
                     GtkTreeIter  *iter,
                     gpointer      data)
{
	DBusGProxy *proxy = NULL;
	GValue value = { 0, };

	gtk_tree_model_get (model, iter,
			    BLUETOOTH_COLUMN_PROXY, &proxy, -1);
	if (proxy == NULL)
		return FALSE;

	g_value_init (&value, G_TYPE_BOOLEAN);
	g_value_set_boolean (&value, TRUE);

	dbus_g_proxy_call_no_reply (proxy, "SetProperty",
				    G_TYPE_STRING, "Powered",
				    G_TYPE_VALUE, &value,
				    G_TYPE_INVALID,
				    G_TYPE_INVALID);

	g_value_unset (&value);
	g_object_unref (proxy);

	return FALSE;
}

static void
set_adapter_powered (BluetoothApplet* self)
{
	GtkTreeModel *adapters;

	adapters = bluetooth_client_get_adapter_model (self->client);
	gtk_tree_model_foreach (adapters, set_powered_foreach, NULL);
	g_object_unref (adapters);
}

static gboolean
device_has_uuid (const char **uuids, const char *uuid)
{
	guint i;

	if (uuids == NULL)
		return FALSE;

	for (i = 0; uuids[i] != NULL; i++) {
		if (g_str_equal (uuid, uuids[i]) != FALSE)
			return TRUE;
	}
	return FALSE;
}

static void
killswitch_state_change (BluetoothKillswitch *kill_switch, KillswitchState state, gpointer user_data)
{
  BluetoothApplet *self = BLUETOOTH_APPLET (user_data);

  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_KILLSWITCH_STATE]);
}

typedef struct {
	BluetoothApplet* self;
	BluetoothAppletConnectFunc func;
	gpointer user_data;
} ConnectionClosure;

static void
connection_callback (BluetoothClient* client, gboolean success, gpointer data)
{
	ConnectionClosure *closure = (ConnectionClosure*) data;

	(*(closure->func)) (closure->self, success, closure->user_data);

	g_free (closure);
}

/**
 * bluetooth_applet_connect_device:
 *
 * @applet: a #BluetoothApplet
 * @device: the device to connect
 * @func: (scope async): a completion callback
 * @data: user data
 */
gboolean
bluetooth_applet_connect_device (BluetoothApplet* applet,
                                 const char* device,
                                 BluetoothAppletConnectFunc func,
                                 gpointer data)
{
	ConnectionClosure *closure;

	g_return_val_if_fail (BLUETOOTH_IS_APPLET (applet), FALSE);
	g_return_val_if_fail (device != NULL, FALSE);
	g_return_val_if_fail (func != NULL, FALSE);

	closure = g_new (ConnectionClosure, 1);
	closure->self = applet;
	closure->func = func;
	closure->user_data = data;

	return bluetooth_client_connect_service (applet->client, device, connection_callback, closure);
}

/**
 * bluetooth_applet_disconnect_device:
 *
 * @applet: a #BluetoothApplet
 * @device: the device to disconnect
 * @func: (scope async): a completion callback
 * @data: user data
 */
gboolean
bluetooth_applet_disconnect_device (BluetoothApplet* applet,
                                 const char* device,
                                 BluetoothAppletConnectFunc func,
                                 gpointer data)
{
	ConnectionClosure *closure;

	g_return_val_if_fail (BLUETOOTH_IS_APPLET (applet), FALSE);
	g_return_val_if_fail (device != NULL, FALSE);
	g_return_val_if_fail (func != NULL, FALSE);

	closure = g_new (ConnectionClosure, 1);
	closure->self = applet;
	closure->func = func;
	closure->user_data = data;

	return bluetooth_client_disconnect_service (applet->client, device, connection_callback, closure);
}

/**
 * bluetooth_applet_get_discoverable:
 *
 * @self: a #BluetoothApplet
 *
 * Returns: TRUE if the default adapter is discoverable, false otherwise
 */
gboolean
bluetooth_applet_get_discoverable (BluetoothApplet* self)
{
	gboolean res = FALSE;

	g_return_val_if_fail (BLUETOOTH_IS_APPLET (self), FALSE);

	if (self->default_adapter == NULL)
		return FALSE;

	gtk_tree_model_get (self->client_model, self->default_adapter,
			BLUETOOTH_COLUMN_DISCOVERABLE, &res,
			-1);

	return res;
}

/**
 * bluetooth_applet_set_discoverable:
 *
 * @self: a #BluetoothApplet
 * @disc:
 */
void
bluetooth_applet_set_discoverable (BluetoothApplet* self, gboolean disc)
{
	g_return_if_fail (BLUETOOTH_IS_APPLET (self));

	bluetooth_client_set_discoverable (self->client, disc, 0);
}

/**
 * bluetooth_applet_get_killswitch_state:
 *
 * @self: a #BluetoothApplet
 *
 * Returns: the state of the killswitch, if one is present, or BLUETOOTH_KILLSWITCH_STATE_NO_ADAPTER otherwise
 */
BluetoothKillswitchState
bluetooth_applet_get_killswitch_state (BluetoothApplet* self)
{

	g_return_val_if_fail (BLUETOOTH_IS_APPLET (self), BLUETOOTH_KILLSWITCH_STATE_NO_ADAPTER);

	if (bluetooth_killswitch_has_killswitches (self->killswitch_manager))
		return bluetooth_killswitch_get_state (self->killswitch_manager);
	else
		return BLUETOOTH_KILLSWITCH_STATE_NO_ADAPTER;
}

/**
 * bluetooth_applet_set_killswitch_state:
 *
 * @self: a #BluetoothApplet
 * @state: the new state
 *
 * Returns: TRUE if the operation could be performed, FALSE otherwise
 */
gboolean
bluetooth_applet_set_killswitch_state (BluetoothApplet* self, BluetoothKillswitchState state)
{

	g_return_val_if_fail (BLUETOOTH_IS_APPLET (self), FALSE);

	if (bluetooth_killswitch_has_killswitches (self->killswitch_manager)) {
		bluetooth_killswitch_set_state (self->killswitch_manager, state);
		return TRUE;
	} else
		return FALSE;
}

/**
 * bluetooth_applet_get_show_full_menu:
 *
 * @self: a #BluetoothApplet
 *
 * Returns: TRUE if the full menu is to be shown, FALSE otherwise
 * (full menu includes device submenus and global actions)
 */
gboolean
bluetooth_applet_get_show_full_menu (BluetoothApplet* self)
{

	g_return_val_if_fail (BLUETOOTH_IS_APPLET (self), FALSE);

	if (self->num_adapters_present == 0)
		return FALSE;
	else
		/* the original code had <=, but does it make sense to have less
		 * adapters at all than adapters powered? */
		return (self->num_adapters_present == self->num_adapters_powered) &&
               (bluetooth_applet_get_killswitch_state(self) == BLUETOOTH_KILLSWITCH_STATE_UNBLOCKED);
}

/**
 * bluetooth_applet_get_devices:
 *
 * @self: a #BluetoothApplet
 *
 * Returns: (element-type GnomeBluetoothApplet.SimpleDevice) (transfer full): Returns the devices which should be shown to the user
 */
GList*
bluetooth_applet_get_devices (BluetoothApplet* self)
{
	GList* result = NULL;
	GtkTreeIter iter;
	gboolean cont;

	g_return_val_if_fail (BLUETOOTH_IS_APPLET (self), NULL);

	if(self->default_adapter == NULL) // no adapter
		return NULL;

	cont = gtk_tree_model_iter_children (self->client_model, &iter, self->default_adapter);
	while (cont) {
		BluetoothSimpleDevice* dev = g_new (BluetoothSimpleDevice, 1);
		GHashTable *services;
		DBusGProxy *proxy;
		char **uuids;

		gtk_tree_model_get (self->client_model, &iter,
				BLUETOOTH_COLUMN_ADDRESS, &dev->bdaddr,
				BLUETOOTH_COLUMN_PROXY, &proxy,
				BLUETOOTH_COLUMN_SERVICES, &services,
				BLUETOOTH_COLUMN_ALIAS, &dev->alias,
				BLUETOOTH_COLUMN_UUIDS, &uuids,
				BLUETOOTH_COLUMN_TYPE, &dev->type,
				-1);

		dev->device_path = g_strdup (dbus_g_proxy_get_path (proxy));
		g_object_unref (proxy);

		/* If one service is connected, then we're connected */
		dev->connected = FALSE;
		dev->can_connect = FALSE;
		if (services != NULL) {
			dev->can_connect = TRUE;
			GList *list, *l;
			list = g_hash_table_get_values (services);
			for (l = list; l != NULL; l = l->next) {
				BluetoothStatus val = GPOINTER_TO_INT (l->data);
				if (val == BLUETOOTH_STATUS_CONNECTED ||
					val == BLUETOOTH_STATUS_PLAYING) {
						dev->connected = TRUE;
						break;
				}
			}
			g_list_free (list);
		}

		dev->capabilities = 0;
		dev->capabilities |= device_has_uuid ((const char **) uuids, "OBEXObjectPush") ? BLUETOOTH_CAPABILITIES_OBEX_PUSH : 0;
		dev->capabilities |= device_has_uuid ((const char **) uuids, "OBEXFileTransfer") ? BLUETOOTH_CAPABILITIES_OBEX_FILE_TRANSFER : 0;

		result = g_list_prepend (result, dev);
		if (services != NULL)
		  g_hash_table_unref (services);
		g_strfreev (uuids);

		cont = gtk_tree_model_iter_next(self->client_model, &iter);
	}
	result = g_list_reverse (result);

	return result;
}

static void
bluetooth_applet_get_property (GObject* self, guint property_id, GValue* value, GParamSpec* pspec)
{
	switch (property_id) {
	case PROP_FULL_MENU:
		g_value_set_boolean (value, bluetooth_applet_get_show_full_menu (BLUETOOTH_APPLET (self)));
		return;
	case PROP_KILLSWITCH_STATE:
		g_value_set_int (value, bluetooth_applet_get_killswitch_state (BLUETOOTH_APPLET (self)));
		return;
	case PROP_DISCOVERABLE:
		g_value_set_boolean (value, bluetooth_applet_get_discoverable (BLUETOOTH_APPLET (self)));
		return;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (self, property_id, pspec);
	}
}

static void
bluetooth_applet_set_property (GObject* gobj, guint property_id, const GValue* value, GParamSpec* pspec)
{
	BluetoothApplet *self = BLUETOOTH_APPLET (gobj);

	switch (property_id) {
	case PROP_KILLSWITCH_STATE:
		bluetooth_applet_set_killswitch_state (self, g_value_get_int (value));
		return;
	case PROP_DISCOVERABLE:
		bluetooth_applet_set_discoverable (self, g_value_get_boolean (value));
		return;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (self, property_id, pspec);
	}
}

static void
bluetooth_applet_init (BluetoothApplet *self)
{
	GObject* gobject_client_model = NULL;

	self->client = bluetooth_client_new ();
	self->client_model = bluetooth_client_get_model (self->client);

	self->default_adapter = NULL;
	self->agent = bluetooth_agent_new ();

	self->killswitch_manager = bluetooth_killswitch_new();
	g_signal_connect (self->killswitch_manager, "state-changed", G_CALLBACK(killswitch_state_change), self);

	self->pending_requests = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);
	dbus_g_error_domain_register(AGENT_ERROR, "org.bluez.Error", AGENT_ERROR_TYPE);

	/* Make sure all the unblocked adapters are powered,
	 * so as to avoid seeing unpowered, but unblocked
	 * devices */
	set_adapter_powered (self);
	find_default_adapter (self);

	gobject_client_model = G_OBJECT (self->client_model);
	g_signal_connect(gobject_client_model, "row-inserted",
			G_CALLBACK(device_added_or_changed), self);
	g_signal_connect(gobject_client_model, "row-deleted",
			G_CALLBACK(device_removed), self);
	g_signal_connect (gobject_client_model, "row-changed",
			G_CALLBACK (device_added_or_changed), self);
}

static void
bluetooth_applet_dispose (GObject* self)
{

	BluetoothApplet* applet = BLUETOOTH_APPLET (self);

	if (applet->client) {
		g_object_unref (applet->client);
		applet->client = NULL;
	}

	if (applet->killswitch_manager) {
		g_object_unref (applet->killswitch_manager);
		applet->killswitch_manager = NULL;
	}

	if (applet->client_model) {
		g_object_unref (applet->client_model);
		applet->client_model = NULL;
	}

	if (applet->agent) {
		bluetooth_agent_unregister (applet->agent);
		g_object_unref (applet->agent);
		applet->agent = NULL;
	}
}

static void
bluetooth_applet_class_init (BluetoothAppletClass *klass)
{
	GObjectClass* gobject_class = G_OBJECT_CLASS (klass);

	gobject_class->dispose = bluetooth_applet_dispose;
	gobject_class->get_property = bluetooth_applet_get_property;
	gobject_class->set_property = bluetooth_applet_set_property;

	/* should be enum, but KillswitchState is not registered */
	properties[PROP_KILLSWITCH_STATE] = g_param_spec_int ("killswitch-state",
			"Killswitch state",
			"State of Bluetooth hardware switches",
			KILLSWITCH_STATE_NO_ADAPTER, KILLSWITCH_STATE_HARD_BLOCKED, KILLSWITCH_STATE_NO_ADAPTER, G_PARAM_READABLE | G_PARAM_WRITABLE);
	g_object_class_install_property (gobject_class, PROP_KILLSWITCH_STATE, properties[PROP_KILLSWITCH_STATE]);

	properties[PROP_DISCOVERABLE] = g_param_spec_boolean ("discoverable",
			"Adapter visibility",
			"Wheter the adapter is visible or not",
			FALSE, G_PARAM_READABLE | G_PARAM_WRITABLE);
	g_object_class_install_property (gobject_class, PROP_DISCOVERABLE, properties[PROP_DISCOVERABLE]);

	properties[PROP_FULL_MENU] = g_param_spec_boolean ("show-full-menu",
			"Show the full applet menu",
			"Show actions related to the adapter and other miscellanous in the main menu",
			TRUE, G_PARAM_READABLE);
	g_object_class_install_property (gobject_class, PROP_FULL_MENU, properties[PROP_FULL_MENU]);

	signals[SIGNAL_DEVICES_CHANGED] = g_signal_new ("devices-changed", G_TYPE_FROM_CLASS (gobject_class),
			G_SIGNAL_RUN_FIRST | G_SIGNAL_NO_RECURSE, 0, NULL, NULL, g_cclosure_marshal_VOID__VOID,
			G_TYPE_NONE, 0);

	signals[SIGNAL_PINCODE_REQUEST] = g_signal_new ("pincode-request", G_TYPE_FROM_CLASS (gobject_class),
			G_SIGNAL_RUN_FIRST, 0, NULL, NULL, marshal_VOID__STRING_STRING_STRING_BOOLEAN,
			G_TYPE_NONE, 4, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_BOOLEAN);

	signals[SIGNAL_CONFIRM_REQUEST] = g_signal_new ("confirm-request", G_TYPE_FROM_CLASS (gobject_class),
			G_SIGNAL_RUN_FIRST, 0, NULL, NULL, marshal_VOID__STRING_STRING_STRING_UINT,
			G_TYPE_NONE, 4, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_UINT);

	signals[SIGNAL_AUTHORIZE_REQUEST] = g_signal_new ("auth-request", G_TYPE_FROM_CLASS (gobject_class),
			G_SIGNAL_RUN_FIRST, 0, NULL, NULL, marshal_VOID__STRING_STRING_STRING_STRING,
			G_TYPE_NONE, 4, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);

	signals[SIGNAL_CANCEL_REQUEST] = g_signal_new ("cancel-request", G_TYPE_FROM_CLASS (gobject_class),
			G_SIGNAL_RUN_FIRST, 0, NULL, NULL, g_cclosure_marshal_VOID__VOID,
			G_TYPE_NONE, 0);
}
