/*  XMMS2 - X Music Multiplexer System
 *  Copyright (C) 2003-2006 XMMS2 Team
 *
 *  PLUGINS ARE NOT CONSIDERED TO BE DERIVED WORK !!!
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
 */

#include "xmms/xmms_defs.h"
#include "xmmspriv/xmms_plugin.h"
#include "xmms/xmms_config.h"
#include "xmms/xmms_object.h"
#include "xmms/xmms_log.h"
#include "xmmspriv/xmms_playlist.h"
#include "xmmspriv/xmms_outputplugin.h"
#include "xmmspriv/xmms_plsplugins.h"
#include "xmmspriv/xmms_xform.h"

#include <gmodule.h>
#include <string.h>
#include <stdarg.h>

#ifdef HAVE_VALGRIND
# include <memcheck.h>
#endif

#ifdef XMMS_OS_DARWIN
# define XMMS_LIBSUFFIX ".dylib"
#elif XMMS_OS_HPUX11 && !defined(__LP64__)
# define XMMS_LIBSUFFIX ".sl"
#else
# define XMMS_LIBSUFFIX ".so"
#endif

typedef struct {
	gchar *key;
	gchar *value;
} xmms_plugin_info_t;

extern xmms_config_t *global_config;

/*
 * Global variables
 */
static GList *xmms_plugin_list;

/*
 * Function prototypes
 */
static gboolean xmms_plugin_setup (xmms_plugin_t *plugin, xmms_plugin_desc_t *desc);
static gboolean xmms_plugin_load (xmms_plugin_desc_t *desc, GModule *module);

/*
 * Public functions
 */

/**
 * @defgroup XMMSPlugin XMMSPlugin
 * @brief Functions used when working with XMMS2 plugins in general.
 *
 * Every plugin has an init function that should be defined as follows:
 * @code
 * xmms_plugin_t *xmms_plugin_get (void)
 * @endcode
 *
 * This function must call #xmms_plugin_new with the appropiate arguments.
 * This function can also call #xmms_plugin_info_add, #xmms_plugin_method_add,
 * #xmms_plugin_properties_add
 *
 * A example plugin here is:
 * @code
 * xmms_plugin_t *
 * xmms_plugin_get (void) {
 * 	xmms_plugin_t *plugin;
 *	
 *	plugin = xmms_plugin_new (XMMS_PLUGIN_TYPE_EXAMPLE, "test",
 *	                          "Test Plugin",
 *	                          XMMS_VERSION,
 *	                          "A very simple plugin");
 *	xmms_plugin_info_add (plugin, "Author", "Karsten Brinkmann");
 *	xmms_plugin_method_add (plugin, XMMS_PLUGIN_METHOD_TEST, xmms_test);
 *	return plugin;
 * }
 * @endcode
 *
 * @if api_plugin
 * @{
 */

/**
 * Add information to the plugin. This information can be
 * viewed in a client. The information can be for example
 * the name of the author or the webpage of the plugin.
 *
 * @param plugin The plugin to set the info in.
 * @param key This can be any given string.
 * @param value Value of this key.
 */
void
xmms_plugin_info_add (xmms_plugin_t *plugin, gchar *key, gchar *value)
{
	xmms_plugin_info_t *info;

	g_return_if_fail (plugin);
	g_return_if_fail (key);
	g_return_if_fail (value);

	info = g_new0 (xmms_plugin_info_t, 1);
	info->key = g_strdup (key);
	info->value = g_strdup (value);

	plugin->info_list = g_list_append (plugin->info_list, info);
}

/**
 * Lookup the value of a plugin's config property, given the property key.
 * @param[in] plugin The plugin
 * @param[in] key The property key (config path)
 * @return A config value
 * @todo config value <-> property fixup
 */
xmms_config_property_t *
xmms_plugin_config_lookup (xmms_plugin_t *plugin,
                           const gchar *key)
{
	gchar path[256];
	xmms_config_property_t *prop;

	g_return_val_if_fail (plugin, NULL);
	g_return_val_if_fail (key, NULL);
	
	g_snprintf (path, sizeof (path), "%s.%s",
	            xmms_plugin_shortname_get (plugin), key);
	prop = xmms_config_lookup (path);

	return prop;
}

/**
 * Register a config property for a plugin.
 * @param[in] plugin The plugin
 * @param[in] name The property name
 * @param[in] default_value The default value for the property
 * @param[in] cb A callback function to be executed when the property value
 * changes
 * @param[in] userdata Pointer to data to be passed to the callback
 * @todo config value <-> property fixup
 */
xmms_config_property_t *
xmms_plugin_config_property_register (xmms_plugin_t *plugin,
                                      const gchar *name,
                                      const gchar *default_value,
                                      xmms_object_handler_t cb,
                                      gpointer userdata)
{
	gchar fullpath[256];
	xmms_config_property_t *prop;

	g_return_val_if_fail (plugin, NULL);
	g_return_val_if_fail (name, NULL);
	g_return_val_if_fail (default_value, NULL);

	g_snprintf (fullpath, sizeof (fullpath), "%s.%s",
	            xmms_plugin_shortname_get (plugin), name);

	prop = xmms_config_property_register (fullpath, default_value, cb,
	                                      userdata);

	return prop;
}

/**
 * @}
 * -- end of plugin API section --
 * @endif
 *
 * @if internal
 * -- internal documentation section --
 * @addtogroup XMMSPlugin
 * @{
 */

/**
 * @internal Get the type of this plugin
 * @param[in] plugin The plugin
 * @return The plugin type (#xmms_plugin_type_t)
 */
xmms_plugin_type_t
xmms_plugin_type_get (const xmms_plugin_t *plugin)
{
	g_return_val_if_fail (plugin, 0);
	
	return plugin->type;
}

/**
 * @internal Get the plugin's name. This is just an accessor method.
 * @param[in] plugin The plugin
 * @return A string containing the plugin's name
 */
const char *
xmms_plugin_name_get (const xmms_plugin_t *plugin)
{
	g_return_val_if_fail (plugin, NULL);

	return plugin->name;
}

/**
 * @internal Get the plugin's short name. This is just an accessor method.
 * @param[in] plugin The plugin
 * @return A string containing the plugin's short name
 */
const gchar *
xmms_plugin_shortname_get (const xmms_plugin_t *plugin)
{
	g_return_val_if_fail (plugin, NULL);

	return plugin->shortname;
}

/**
 * @internal Get the plugin's version. This is just an accessor method.
 * @param[in] plugin The plugin
 * @return A string containing the plugin's version
 */
const gchar *
xmms_plugin_version_get (const xmms_plugin_t *plugin)
{
	g_return_val_if_fail (plugin, NULL);

	return plugin->version;
}

/**
 * @internal Get the plugin's description. This is just an accessor method.
 * @param[in] plugin The plugin
 * @return A string containing the plugin's description
 */
const char *
xmms_plugin_description_get (const xmms_plugin_t *plugin)
{
	g_return_val_if_fail (plugin, NULL);

	return plugin->description;
}

/**
 * @internal Get info from the plugin.
 * @param[in] plugin The plugin
 * @return a GList of info from the plugin
 */
const GList*
xmms_plugin_info_get (const xmms_plugin_t *plugin)
{
	g_return_val_if_fail (plugin, NULL);

	return plugin->info_list;
}

/*
 * Private functions
 */


static void
xmms_plugin_add_builtin_plugins (void)
{
	extern xmms_plugin_desc_t xmms_builtin_ringbuf;
	extern xmms_plugin_desc_t xmms_builtin_magic;
	extern xmms_plugin_desc_t xmms_builtin_converter;

	xmms_plugin_load (&xmms_builtin_ringbuf, NULL);
	xmms_plugin_load (&xmms_builtin_magic, NULL);
	xmms_plugin_load (&xmms_builtin_converter, NULL);
}


/**
 * @internal Initialise the plugin system
 * @param[in] path Absolute path to the plugins directory.
 * @return Whether the initialisation was successful or not.
 */
gboolean
xmms_plugin_init (gchar *path)
{
	if (!path)
		path = PKGLIBDIR;

	xmms_plugin_scan_directory (path);

	xmms_plugin_add_builtin_plugins ();
	return TRUE;
}

/**
 * @internal Shut down the plugin system. This function unrefs all the plugins
 * loaded.
 */
void
xmms_plugin_shutdown ()
{
	GList *n;
#ifdef HAVE_VALGRIND
	/* print out a leak summary at this point, because the final leak
	 * summary won't include proper backtraces of leaks found in
	 * plugins, since we close the so's here.
	 *
	 * note: the following call doesn't do anything if we're not run
	 * in valgrind
	 */
	VALGRIND_DO_LEAK_CHECK
		;
#endif
	
	for (n = xmms_plugin_list; n; n = g_list_next (n)) {
		xmms_plugin_t *p = n->data;

		/* if this plugin's refcount is > 1, then there's a bug
		 * in one of the other subsystems
		 */
		if (p->object.ref > 1) {
			XMMS_DBG ("%s's refcount is %i",
			          p->name, p->object.ref);
		}

		xmms_object_unref (p);
	}
}


static gboolean
xmms_plugin_load (xmms_plugin_desc_t *desc, GModule *module)
{
	xmms_plugin_t *plugin;
	xmms_plugin_t *(*allocer) ();
	gboolean (*verifier) (xmms_plugin_t *);
	gint expected_ver;

	switch (desc->type) {
	case XMMS_PLUGIN_TYPE_OUTPUT:
		expected_ver = XMMS_OUTPUT_API_VERSION;
		allocer = xmms_output_plugin_new;
		verifier = xmms_output_plugin_verify;
		break;
/*
	case XMMS_PLUGIN_TYPE_PLAYLIST:
		expected_ver = XMMS_PLAYLIST_API_VERSION;
		initer = xmms_playlist_plugin_init;
		break;
*/
	case XMMS_PLUGIN_TYPE_XFORM:
		expected_ver = XMMS_XFORM_API_VERSION;
		allocer = xmms_xform_plugin_new;
		verifier = xmms_xform_plugin_verify;
		break;
	default:
		XMMS_DBG ("Unknown plugin type!");
		return FALSE;
	}

	if (desc->api_version != expected_ver) {
		XMMS_DBG ("Bad api version!");
		return FALSE;
	}

	plugin = allocer ();
	if (!plugin) {
		XMMS_DBG ("Alloc failed!");
		return FALSE;
	}

	if (!xmms_plugin_setup (plugin, desc)) {
		XMMS_DBG ("Setup failed!");
		xmms_object_unref (plugin);
		return FALSE;
	}

	if (!desc->setup_func (plugin)) {
		XMMS_DBG ("Plugin setup failed!");
		xmms_object_unref (plugin);
		return FALSE;
	}

	if (!verifier (plugin)) {
		XMMS_DBG ("Verify failed!");
		xmms_object_unref (plugin);
		return FALSE;
	}

	plugin->module = module;

	xmms_plugin_list = g_list_prepend (xmms_plugin_list, plugin);
	return TRUE;
}

/**
 * @internal Scan a particular directory for plugins to load
 * @param[in] dir Absolute path to plugins directory
 * @return TRUE if directory successfully scanned for plugins
 */
gboolean
xmms_plugin_scan_directory (const gchar *dir)
{
	GDir *d;
	const char *name;
	gchar *path;
	GModule *module;
	gpointer sym;

	g_return_val_if_fail (global_config, FALSE);

	XMMS_DBG ("Scanning directory: %s", dir);
	
	d = g_dir_open (dir, 0, NULL);
	if (!d) {
		xmms_log_error ("Failed to open plugin directory (%s)", dir);
		return FALSE;
	}

	while ((name = g_dir_read_name (d))) {
		if (strncmp (name, "lib", 3) != 0)
			continue;

		if (!strstr (name, XMMS_LIBSUFFIX))
			continue;

		path = g_build_filename (dir, name, NULL);
		if (!g_file_test (path, G_FILE_TEST_IS_REGULAR)) {
			g_free (path);
			continue;
		}

		XMMS_DBG ("Trying to load file: %s", path);
		module = g_module_open (path, 0);
		if (!module) {
			xmms_log_error ("Failed to open plugin %s: %s",
			                path, g_module_error ());
			g_free (path);
			continue;
		}

		g_free (path);

		if (!g_module_symbol (module, "XMMS_PLUGIN_DESC", &sym)) {
			g_module_close (module);
			continue;
		}

		if (!xmms_plugin_load ((xmms_plugin_desc_t *)sym, module)) {
			g_module_close (module);
		}
	}

	g_dir_close (d);

	return TRUE;
}

GList *
xmms_plugin_client_list (xmms_object_t *main, guint32 type, xmms_error_t *err)
{
	GList *list = NULL, *node, *l;

	l = xmms_plugin_list_get (type);

	for (node = l; node; node = g_list_next (node)) {
		GHashTable *hash;
		const GList *p;
		xmms_plugin_t *plugin = node->data;

		hash = g_hash_table_new (g_str_hash, g_str_equal);
		g_hash_table_insert (hash, "name",
		                     xmms_object_cmd_value_str_new (xmms_plugin_name_get (plugin)));
		g_hash_table_insert (hash, "shortname",
		                     xmms_object_cmd_value_str_new (xmms_plugin_shortname_get (plugin)));
		g_hash_table_insert (hash, "version",
		                     xmms_object_cmd_value_str_new (xmms_plugin_version_get (plugin)));
		g_hash_table_insert (hash, "description",
		                     xmms_object_cmd_value_str_new (xmms_plugin_description_get (plugin)));
		g_hash_table_insert (hash, "type",
		                     xmms_object_cmd_value_uint_new (xmms_plugin_type_get (plugin)));

		for (p = xmms_plugin_info_get (plugin); p; p = g_list_next (p)) {
			xmms_plugin_info_t *info = p->data;
			g_hash_table_insert (hash, info->key, xmms_object_cmd_value_str_new (info->value));
		}

		list = g_list_prepend (list, xmms_object_cmd_value_dict_new (hash));

	}

	xmms_plugin_list_destroy (l);

	return list;
}

void
xmms_plugin_foreach (xmms_plugin_type_t type, xmms_plugin_foreach_func_t func, gpointer user_data)
{
	GList *node;
	
	for (node = xmms_plugin_list; node; node = g_list_next (node)) {
		xmms_plugin_t *plugin = node->data;
		
		if (plugin->type == type || type == XMMS_PLUGIN_TYPE_ALL) {
			if (!func (plugin, user_data))
				break;
		}
	}	
}

/**
 * @internal Look for loaded plugins matching a particular type
 * @param[in] type The plugin type to look for. (#xmms_plugin_type_t)
 * @return List of loaded plugins matching type
 */
GList *
xmms_plugin_list_get (xmms_plugin_type_t type)
{
	GList *list = NULL, *node;

	for (node = xmms_plugin_list; node; node = g_list_next (node)) {
		xmms_plugin_t *plugin = node->data;

		if (plugin->type == type || type == XMMS_PLUGIN_TYPE_ALL) {
			xmms_object_ref (plugin);
			list = g_list_prepend (list, plugin);
		}
	}
	
	return list;
}

/**
 * @internal Destroy a list of plugins. Note: this is not used to destroy the
 * global plugin list.
 * @param[in] list The plugin list to destroy
 */
void
xmms_plugin_list_destroy (GList *list)
{
	while (list) {
		xmms_object_unref (list->data);
		list = g_list_delete_link (list, list);
	}
}

/**
 * @internal Find a plugin that's been loaded, by a particular type and name
 * @param[in] type The type of plugin to look for
 * @param[in] name The name of the plugin to look for
 * @return The plugin instance, if found. NULL otherwise.
 */
xmms_plugin_t *
xmms_plugin_find (xmms_plugin_type_t type, const gchar *name)
{
	xmms_plugin_t *ret = NULL;
	GList *l;

	g_return_val_if_fail (name, NULL);

	for (l = xmms_plugin_list; l; l = l->next) {
		xmms_plugin_t *plugin = l->data;

		if (plugin->type == type &&
		    !g_strcasecmp (plugin->shortname, name)) {
			ret = plugin;
			xmms_object_ref (ret);

			break;
		}
	}

	return ret;
}


static gboolean
xmms_plugin_setup (xmms_plugin_t *plugin, xmms_plugin_desc_t *desc)
{
	plugin->type = desc->type;
	plugin->shortname = desc->shortname;
	plugin->name = desc->name;
	plugin->version = desc->version;
	plugin->description = desc->description;

	return TRUE;
}

void
xmms_plugin_destroy (xmms_plugin_t *plugin)
{
	g_list_free (plugin->info_list);
	if (plugin->module)
		g_module_close (plugin->module);
	xmms_object_unref (plugin);
}
