/*
 *
 *  BlueZ - Bluetooth protocol stack for Linux
 *
 *  Copyright (C) 2005-2008  Marcel Holtmann <marcel@holtmann.org>
 *
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2.1 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#ifndef __BLUETOOTH_CLIENT_PRIVATE_H
#define __BLUETOOTH_CLIENT_PRIVATE_H

#include <glib-object.h>
#include <gtk/gtk.h>
#include <bluetooth-enums.h>

G_BEGIN_DECLS

typedef void (*BluetoothClientSetupFunc) (BluetoothClient *client,
					  const GError    *error,
					  const char      *device_path);

gboolean bluetooth_client_setup_device (BluetoothClient          *client,
					const char               *device_path,
					const char               *agent,
					BluetoothClientSetupFunc  func,
					gboolean                  pair);

gboolean bluetooth_client_set_trusted(BluetoothClient *client,
					const char *device, gboolean trusted);

GDBusProxy *bluetooth_client_get_device (BluetoothClient *client,
					 const char      *path);

void bluetooth_client_dump_device (GtkTreeModel *model,
				   GtkTreeIter *iter);

gboolean bluetooth_client_get_connectable(const char **uuids);

G_END_DECLS

#endif /* __BLUETOOTH_CLIENT_PRIVATE_H */
