/*  XMMS2 - X Music Multiplexer System
 *  Copyright (C) 2003-2023 XMMS2 Team
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

#include <xmms/xmms_xformplugin.h>
#include <xmms/xmms_log.h>

#include <glib.h>
#include <libxml/xmlreader.h>

#define TEMP_BUF_MAX_SIZE 4096

typedef enum {
	RSS,
	CHANNEL,
	ITEM,
	ITEM_TITLE,
} _navigation_state_key;

typedef struct xmms_rss_data_St {
	xmms_xform_t *xform;
	xmms_error_t *error;
	gboolean parse_failure;
	_navigation_state_key nav_state;
	char item_title[TEMP_BUF_MAX_SIZE];
	xmlChar *item_url;
} xmms_rss_data_t;

static gboolean xmms_rss_plugin_setup (xmms_xform_plugin_t *xform_plugin);
static gboolean xmms_rss_browse (xmms_xform_t *xform, const gchar *url, xmms_error_t *error);
static void xmms_rss_destroy (xmms_xform_t *xform);

XMMS_XFORM_PLUGIN_DEFINE ("rss",
                          "reader for rss podcasts",
                          XMMS_VERSION,
                          "reader for rss podcasts",
                          xmms_rss_plugin_setup);

static gboolean
xmms_rss_plugin_setup (xmms_xform_plugin_t *xform_plugin)
{
	xmms_xform_methods_t methods;

	XMMS_XFORM_METHODS_INIT (methods);
	methods.browse = xmms_rss_browse;
    methods.destroy = xmms_rss_destroy;

	xmms_xform_plugin_methods_set (xform_plugin, &methods);

	xmms_xform_plugin_indata_add (xform_plugin,
	                              XMMS_STREAM_TYPE_MIMETYPE,
	                              "application/x-xmms2-xml+rss",
	                              XMMS_STREAM_TYPE_END);
	xmms_xform_plugin_indata_add (xform_plugin,
	                              XMMS_STREAM_TYPE_MIMETYPE,
	                              "application/rss+xml*",
	                              XMMS_STREAM_TYPE_END);
	xmms_xform_plugin_set_out_stream_type (xform_plugin,
	                                       XMMS_STREAM_TYPE_MIMETYPE,
	                                       "application/x-xmms2-playlist-entries",
	                                       XMMS_STREAM_TYPE_END);

	xmms_magic_add ("rss tag", "application/x-xmms2-xml+rss",
	                "0 string/c <rss ",
	                NULL);
	xmms_magic_extension_add ("application/xml", "*.rss");

	return TRUE;
}

static void
xmms_rss_start_element (xmms_rss_data_t *data, const xmlChar *name,
                        const xmlChar **attrs)
{
	int i;

	XMMS_DBG ("start elem %s", name);

	if (!data) {
		return;
	}

	if (xmlStrEqual (name, BAD_CAST "enclosure")) {
		if (attrs) {
			for (i = 0; attrs[i]; i += 2) {
				char *attr;

				if (!xmlStrEqual (attrs[i], BAD_CAST "url"))
					continue;

				attr = (char *) attrs[i + 1];

				XMMS_DBG ("start elem %s: Found url=\"%s\"", name, attr);
				data->item_url = xmlCharStrdup(attr);

				break;
			}
		} else {
			XMMS_DBG ("start elem %s: No attributes", name);
		}
	} else if (xmlStrEqual (name, BAD_CAST "rss")) {
		data->nav_state = RSS;
	} else if (xmlStrEqual (name, BAD_CAST "channel") && data->nav_state == RSS) {
		data->nav_state = CHANNEL;
	} else if (xmlStrEqual (name, BAD_CAST "item") && data->nav_state == CHANNEL) {
		data->nav_state = ITEM;
	} else if (xmlStrEqual (name, BAD_CAST "title") && data->nav_state == ITEM) {
		data->nav_state = ITEM_TITLE;
	}
}

static void
xmms_rss_characters (xmms_rss_data_t *data, const xmlChar *chars, int len)
{
	XMMS_DBG ("characters len=%d: state=%d", len, data->nav_state);
	if (data->nav_state == ITEM_TITLE) {
		int copy_len = (len + 1 > TEMP_BUF_MAX_SIZE)?TEMP_BUF_MAX_SIZE:len;
		memmove(data->item_title, chars, copy_len);
		data->item_title[copy_len] = '\0';
		XMMS_DBG ("characters len=%d: item_title\"%s\"", len, data->item_title);
	}
}

static void
xmms_rss_end_element (xmms_rss_data_t *data, const xmlChar *name)
{
	XMMS_DBG ("end elem %s", name);

	if (!data)
		return;
	xmms_xform_t *xform = data->xform;

	if (xmlStrEqual (name, BAD_CAST "item") && data->nav_state == ITEM) {
		data->nav_state = CHANNEL;
		xmms_xform_browse_add_symlink (xform, NULL, (const gchar *)data->item_url);
		if (data->item_title) {
			xmms_xform_browse_add_entry_property_str (xform, "title", data->item_title);
			free(data->item_url);
		}
		data->item_url = NULL;
	} else if (xmlStrEqual (name, BAD_CAST "title") && data->nav_state == ITEM_TITLE) {
		data->nav_state = ITEM;
	} else if (xmlStrEqual (name, BAD_CAST "channel") && data->nav_state == CHANNEL) {
		data->nav_state = RSS;
	} else {
		XMMS_DBG ("end elem %s: doing nothing at state = %d", name, data->nav_state);
	}
}

static void
xmms_rss_error (xmms_rss_data_t *data, const char *msg, ...)
{
	va_list ap;
	char str[1000];

	va_start (ap, msg);
	vsnprintf (str, sizeof (str), msg, ap);
	va_end (ap);

	data->parse_failure = TRUE;
	xmms_error_set (data->error, XMMS_ERROR_INVAL, str);
}

static gboolean
xmms_rss_browse (xmms_xform_t *xform, const gchar *url, xmms_error_t *error)
{
	int ret;
	char buffer[1024];
	xmlSAXHandler handler;
	xmlParserCtxtPtr ctx;
	xmms_rss_data_t data;

	g_return_val_if_fail (xform, FALSE);

	memset (&handler, 0, sizeof (handler));
	memset (&data, 0, sizeof (data));

	handler.startElement = (startElementSAXFunc) xmms_rss_start_element;
	handler.endElement = (endElementSAXFunc) xmms_rss_end_element;
	handler.characters = (charactersSAXFunc) xmms_rss_characters;
	handler.error = (errorSAXFunc) xmms_rss_error;
	handler.fatalError = (fatalErrorSAXFunc) xmms_rss_error;

	data.xform = xform;
	data.error = error;
	data.parse_failure = FALSE;

	xmms_error_reset (error);

	ctx = xmlCreatePushParserCtxt (&handler, &data, buffer, 0, NULL);
	if (!ctx) {
		xmms_error_set (error, XMMS_ERROR_OOM,
		                "Could not allocate xml parser");
		return FALSE;
	}
	/* Tell libxml2 to replace XML character entities (e.g., changes "&#38;" to
	 * "&"), particularly in attribute values and inner text in the characters
	 * and startElement callbacks
	 */
	ctx->replaceEntities = 1;

	while ((ret = xmms_xform_read (xform, buffer, sizeof (buffer), error)) > 0) {
		xmlParseChunk (ctx, buffer, ret, 0);
	}

	if (ret < 0) {
		xmms_error_set (error, XMMS_ERROR_GENERIC, "xmms_xform_read failed");
		return FALSE;
	}

	if (data.parse_failure)
		return FALSE;

	xmlParseChunk (ctx, buffer, 0, 1);

	xmms_error_reset (error);
	xmlFreeParserCtxt (ctx);

	return TRUE;
}

static void
xmms_rss_destroy (xmms_xform_t *xform)
{
	xmms_rss_data_t *data;

	g_return_if_fail (xform);

	data = xmms_xform_private_data_get (xform);
	g_return_if_fail (data);
	if (data->item_url) {
		free(data->item_url);
	}
}
