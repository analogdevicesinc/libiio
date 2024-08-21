// SPDX-License-Identifier: LGPL-2.1-or-later
/*
 * libiio - Library for interfacing industrial I/O (IIO) devices
 *
 * Copyright (C) 2014 Analog Devices, Inc.
 * Author: Paul Cercueil <paul.cercueil@analog.com>
 */

#include <errno.h>
#include <iio/iio-backend.h>
#include <iio/iio-debug.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <string.h>

#include "iio-private.h"

#define XML_HEADER "<?xml version=\"1.0\""

static struct iio_context *
xml_create_context(const struct iio_context_params *params,
                   const char *xml_file);

static int add_attr_to_channel(struct iio_channel *chn, xmlNode *n) {
  const char *name = NULL, *filename = NULL;
  xmlAttr *attr;

  for (attr = n->properties; attr; attr = attr->next) {
    if (!strcmp((const char *)attr->name, "name")) {
      name = (const char *)attr->children->content;
    } else if (!strcmp((const char *)attr->name, "filename")) {
      filename = (const char *)attr->children->content;
    } else {
      chn_dbg(chn, "Unknown field \'%s\'\n", attr->name);
    }
  }

  if (!name) {
    chn_err(chn, "Incomplete attribute\n");
    return -EINVAL;
  }

  return iio_channel_add_attr(chn, name, filename);
}

static int add_attr_to_device(struct iio_device *dev, xmlNode *n,
                              enum iio_attr_type type) {
  xmlAttr *attr;
  char *name = NULL;

  for (attr = n->properties; attr; attr = attr->next) {
    if (!strcmp((char *)attr->name, "name")) {
      name = (char *)attr->children->content;
    } else {
      dev_dbg(dev, "Unknown field \'%s\'\n", attr->name);
    }
  }

  if (!name) {
    dev_err(dev, "Incomplete attribute\n");
    return -EINVAL;
  }

  return iio_device_add_attr(dev, name, type);
}

static int setup_scan_element(const struct iio_device *dev, xmlNode *n,
                              long *index, struct iio_data_format *fmt) {
  xmlAttr *attr;
  int err;

  for (attr = n->properties; attr; attr = attr->next) {
    const char *name = (const char *)attr->name,
               *content = (const char *)attr->children->content;
    if (!strcmp(name, "index")) {
      char *end;
      long long value;

      errno = 0;
      value = strtoll(content, &end, 0);
      if (end == content || value < 0 || errno == ERANGE)
        return -EINVAL;
      *index = (long)value;
    } else if (!strcmp(name, "format")) {
      char e, s;
      if (strchr(content, 'X')) {
        err =
            iio_sscanf(content, "%ce:%c%u/%uX%u>>%u",
#ifdef _MSC_BUILD
                       &e, (unsigned int)sizeof(e), &s, (unsigned int)sizeof(s),
#else
                       &e, &s,
#endif
                       &fmt->bits, &fmt->length, &fmt->repeat, &fmt->shift);
        if (err != 6)
          return -EINVAL;
      } else {
        fmt->repeat = 1;
        err =
            iio_sscanf(content, "%ce:%c%u/%u>>%u",
#ifdef _MSC_BUILD
                       &e, (unsigned int)sizeof(e), &s, (unsigned int)sizeof(s),
#else
                       &e, &s,
#endif
                       &fmt->bits, &fmt->length, &fmt->shift);
        if (err != 5)
          return -EINVAL;
      }
      fmt->is_be = e == 'b';
      fmt->is_signed = (s == 's' || s == 'S');
      fmt->is_fully_defined =
          (s == 'S' || s == 'U' || fmt->bits == fmt->length);
    } else if (!strcmp(name, "scale")) {
      char *end;
      float value;

      errno = 0;
      value = strtof(content, &end);
      if (end == content || errno == ERANGE) {
        fmt->with_scale = false;
        return -EINVAL;
      }

      fmt->with_scale = true;
      fmt->scale = value;
    } else {
      dev_dbg(dev, "Unknown attribute \'%s\' in <scan-element>\n", name);
    }
  }

  return 0;
}

static int create_channel(struct iio_device *dev, xmlNode *node) {
  xmlAttr *attr;
  struct iio_channel *chn;
  int err = -ENOMEM;
  char *name_ptr = NULL, *id_ptr = NULL;
  bool output = false;
  bool scan_element = false;
  long index = -ENOENT;
  struct iio_data_format format = {0};
  xmlNode *n;

  for (attr = node->properties; attr; attr = attr->next) {
    const char *name = (const char *)attr->name,
               *content = (const char *)attr->children->content;
    if (!strcmp(name, "name")) {
      name_ptr = iio_strdup(content);
      if (!name_ptr)
        goto err_free_name_id;
    } else if (!strcmp(name, "id")) {
      id_ptr = iio_strdup(content);
      if (!id_ptr)
        goto err_free_name_id;
    } else if (!strcmp(name, "type")) {
      if (!strcmp(content, "output"))
        output = true;
      else if (strcmp(content, "input"))
        dev_dbg(dev, "Unknown channel type %s\n", content);
    } else {
      dev_dbg(dev, "Unknown attribute \'%s\' in <channel>\n", name);
    }
  }

  if (!id_ptr) {
    dev_err(dev, "Incomplete <attribute>\n");
    err = -EINVAL;
    goto err_free_name_id;
  }

  for (n = node->children; n; n = n->next) {
    if (!strcmp((char *)n->name, "scan-element")) {
      scan_element = true;
      err = setup_scan_element(dev, n, &index, &format);
      if (err < 0)
        goto err_free_name_id;

      break;
    }
  }

  chn = iio_device_add_channel(dev, index, id_ptr, name_ptr, output,
                               scan_element, &format);
  if (!chn) {
    err = -ENOMEM;
    goto err_free_name_id;
  }

  free(name_ptr);
  free(id_ptr);

  for (n = node->children; n; n = n->next) {
    if (!strcmp((char *)n->name, "attribute")) {
      err = add_attr_to_channel(chn, n);
      if (err < 0)
        return err;
    } else if (strcmp((char *)n->name, "scan-element") &&
               strcmp((char *)n->name, "text")) {
      chn_dbg(chn, "Unknown children \'%s\' in <channel>\n", n->name);
      continue;
    }
  }

  return 0;

err_free_name_id:
  free(name_ptr);
  free(id_ptr);
  return err;
}

static int create_device(struct iio_context *ctx, xmlNode *n) {
  xmlAttr *attr;
  struct iio_device *dev;
  int err = -ENOMEM;
  char *name = NULL, *label = NULL, *id = NULL;

  for (attr = n->properties; attr; attr = attr->next) {
    if (!strcmp((char *)attr->name, "name")) {
      name = iio_strdup((char *)attr->children->content);
      if (!name)
        goto err_free_name_label_id;
    } else if (!strcmp((char *)attr->name, "label")) {
      label = iio_strdup((char *)attr->children->content);
      if (!label)
        goto err_free_name_label_id;
    } else if (!strcmp((char *)attr->name, "id")) {
      id = iio_strdup((char *)attr->children->content);
      if (!id)
        goto err_free_name_label_id;
    } else {
      ctx_dbg(ctx, "Unknown attribute \'%s\' in <device>\n", attr->name);
    }
  }

  if (!id) {
    ctx_err(ctx, "Unable to read device ID\n");
    err = -EINVAL;
    goto err_free_name_label_id;
  }

  dev = iio_context_add_device(ctx, id, name, label);
  if (!dev)
    goto err_free_name_label_id;

  /* Those have been duplicated into the iio_device. */
  free(name);
  free(label);
  free(id);

  for (n = n->children; n; n = n->next) {
    if (!strcmp((char *)n->name, "channel")) {
      err = create_channel(dev, n);
      if (err) {
        dev_perror(dev, err, "Unable to create channel");
        return err;
      }
    } else if (!strcmp((char *)n->name, "attribute")) {
      err = add_attr_to_device(dev, n, IIO_ATTR_TYPE_DEVICE);
      if (err < 0)
        return err;
    } else if (!strcmp((char *)n->name, "debug-attribute")) {
      err = add_attr_to_device(dev, n, IIO_ATTR_TYPE_DEBUG);
      if (err < 0)
        return err;
    } else if (!strcmp((char *)n->name, "buffer-attribute")) {
      err = add_attr_to_device(dev, n, IIO_ATTR_TYPE_BUFFER);
      if (err < 0)
        return err;
    } else if (strcmp((char *)n->name, "text")) {
      dev_dbg(dev, "Unknown children \'%s\' in <device>\n", n->name);
      continue;
    }
  }

  return 0;

err_free_name_label_id:
  free(name);
  free(label);
  free(id);
  return err;
}

const char *get_node_prop_value(xmlNode *node, const char *prop) {
  xmlAttr *attr = node->properties;
  const char *value = NULL;
  while (attr) {
    if (!strcmp((const char *)attr->name, prop)) {
      value = (const char *)attr->children->content;
    }
    attr = attr->next;
  }
  return value;
}

int set_node_prop_value(xmlNode *node, const char *prop, const char *value,
                        size_t len) {
  xmlAttr *attr = node->properties;
  while (attr) {
    if (!strcmp((const char *)attr->name, prop)) {
      xmlNodeSetContent(attr->children, value);
      return 0;
    }
    attr = attr->next;
  }
  return -1;
}

xmlNode *find_node_with_name(xmlNode *node, const char *node_type,
                             const char *name, bool is_output) {
  xmlNode *cur_node = NULL;
  const char *is_output_str = NULL;
  for (cur_node = node; cur_node; cur_node = cur_node->next) {
    if (cur_node->type == XML_ELEMENT_NODE) {

      if (strcmp(cur_node->name, node_type) != 0) {
        continue;
      }
      // Check if it is an output channel
      if (strcmp(node_type, "channel") == 0) {
        is_output_str = get_node_prop_value(cur_node, "type");
        if (is_output_str == NULL) {
          continue;
        }
        bool is_output_cur = strcmp(is_output_str, "output") == 0;
        if (is_output_cur != is_output) {
          continue;
        }
      }

      xmlAttr *attr = cur_node->properties;
      const char *pname = NULL;
      while (attr) {
        if (strcmp(attr->name, "name") == 0) {
          pname = (const char *)attr->children->content;
          if (strcmp(pname, name) == 0) {
            return cur_node;
          }
        }
        // Check id
        if (strcmp(attr->name, "id") == 0) {
          pname = (const char *)attr->children->content;
          if (strcmp(pname, name) == 0) {
            return cur_node;
          }
        }
        attr = attr->next;
      }
    }
  }
  return NULL;
}

xmlNode *get_root_context(xmlDoc *document) {

  // Parse xml for info
  if (document == NULL) {
    printf("TDEBUG: document is NULL\n");
    return -1;
  }

  xmlNode *root = xmlDocGetRootElement(document);
  if (root == NULL) {
    printf("TDEBUG: root is NULL\n");
    return -1;
  }

  unsigned int i = 0;
  for (xmlNode *cur_node = root; cur_node; cur_node = cur_node->next) {
    if (cur_node->type == XML_ELEMENT_NODE) {
      if (strcmp(cur_node->name, "context") == 0) {

        // Verify it has a name attribute
        xmlAttr *attr = cur_node->properties;
        const char *name = NULL;
        const char *value = NULL;
        while (attr) {
          if (!strcmp((const char *)attr->name, "name")) {
            name = (const char *)attr->children->content;
          }
          attr = attr->next;
        }
        if (name != NULL) {
          return cur_node->children;
        }
      }
    }
  }
  return NULL;
}

xmlNode *xml_readwrite_attr(const struct iio_attr *attr, xmlDoc *document) {

  const struct iio_device *dev;
  struct iio_attr_list *attr_list;
  struct iio_attr *attr_tmp;
  const struct iio_context *ctx;
  const struct iio_channel *chn;
  const char *attr_name;
  const char *rvalue;
  xmlNode *attribute_node;

  xmlNode *context_node = get_root_context(document);

  switch (attr->type) {
  case IIO_ATTR_TYPE_DEVICE:
    dev = attr->iio.dev;
    attr_name = attr->name;

    xmlNode *device_node =
        find_node_with_name(context_node, "device", dev->name, false);
    // Check

    attribute_node = find_node_with_name(device_node->children, "attribute",
                                         attr_name, false);

  case IIO_ATTR_TYPE_CHANNEL:

    dev = attr->iio.chn->dev;
    chn = attr->iio.chn;
    attr_name = attr->name;
    bool is_output = chn->is_output;

    xmlNode *device_node_chn =
        find_node_with_name(context_node, "device", dev->name, false);

    xmlNode *channel_node = find_node_with_name(device_node_chn->children,
                                                "channel", chn->id, is_output);

    if (channel_node != NULL) {
      attribute_node = find_node_with_name(channel_node->children, "attribute",
                                           attr_name, false);
      if (attribute_node != NULL) {
        // List all properties of attribute_node
        xmlAttr *attr = attribute_node->properties;
        while (attr) {
          attr = attr->next;
        }
      }
    }

    break;

    // case IIO_ATTR_TYPE_CONTEXT: // Handled already

  case IIO_ATTR_TYPE_DEBUG:

    dev = attr->iio.dev;
    attr_name = attr->name;

    xmlNode *device_node_default =
        find_node_with_name(context_node, "device", dev->name, false);
    if (device_node_default != NULL) {
      attribute_node = find_node_with_name(device_node_default->children,
                                           "debug-attribute", attr_name, false);
    }
    break;

  default:

    break;
  }

  return attribute_node;
}

static ssize_t xml_read_attr(const struct iio_attr *attr, char *dst,
                             size_t len) {

  const struct iio_channel *chn;
  const struct iio_device *dev;
  const struct iio_context *ctx;

  // iio_pointer creation is incomplete when attrs are created... (Bug?)
  // We need to reference the context based on the attribute type to
  // get the right context
  switch (attr->type) {
  case IIO_ATTR_TYPE_DEVICE:
    dev = attr->iio.dev;
    ctx = dev->ctx;
    break;
  case IIO_ATTR_TYPE_CHANNEL:
    chn = attr->iio.chn;
    dev = chn->dev;
    ctx = dev->ctx;
    break;
  case IIO_ATTR_TYPE_CONTEXT:
    ctx = attr->iio.ctx;
    break;
  default:
    break;
  }

  char *xml_raw = ctx->xml_raw;

  xmlDoc *document = xmlReadMemory(xml_raw, strlen(xml_raw), NULL, NULL, 0);

  xmlNode *attribute = xml_readwrite_attr(attr, document);
  const char *value = get_node_prop_value(attribute, "value");

  memcpy(dst, value, strlen(value));
  dst[strlen(value)] = '\0';

  xmlFreeDoc(document);

  return strlen(dst);
}

static ssize_t xml_write_attr(const struct iio_attr *attr, const char *src,
                              size_t len) {

  const struct iio_channel *chn;
  const struct iio_device *dev;
  const struct iio_context *ctx;
  xmlChar *xmlbuff;
  int buffersize;

  // iio_pointer creation is incomplete when attrs are created... (Bug?)
  // We need to reference the context based on the attribute type to
  // get the right context
  switch (attr->type) {
  case IIO_ATTR_TYPE_DEVICE:
    dev = attr->iio.dev;
    ctx = dev->ctx;
    break;
  case IIO_ATTR_TYPE_CHANNEL:
    chn = attr->iio.chn;
    dev = chn->dev;
    ctx = dev->ctx;
    break;
  case IIO_ATTR_TYPE_CONTEXT:
    ctx = attr->iio.ctx;
    break;
  default:
    break;
  }

  xmlDoc *document =
      xmlReadMemory(ctx->xml_raw, strlen(ctx->xml_raw), NULL, NULL, 0);

  xmlNode *attribute = xml_readwrite_attr(attr, document);
  set_node_prop_value(attribute, "value", src, len);

  // Update xml state in context
  xmlDocDumpFormatMemory(document, &xmlbuff, &buffersize, 1);
  free(ctx->xml_raw);
  strcpy(ctx->xml_raw, (char *)xmlbuff);

  xmlFree(xmlbuff);
  xmlFreeDoc(document);
}

////////////////////

static const struct iio_backend_ops xml_ops = {
    .scan = NULL, // Will ignore
    .create = xml_create_context,
    .read_attr = xml_read_attr, // TODO
    .write_attr = NULL,         // TODO
    .get_trigger = NULL,        // TODO
    .set_trigger = NULL,        // TODO
    .shutdown = NULL,           // Will ignore
    .set_timeout = NULL,        // Will ignore

    .create_buffer = NULL, // TODO
    .free_buffer = NULL,   // TODO
    .enable_buffer = NULL, // TODO
    .cancel_buffer = NULL, // TODO

    .readbuf = NULL,  // TODO
    .writebuf = NULL, // TODO

    .create_block = NULL,  // TODO
    .free_block = NULL,    // TODO
    .enqueue_block = NULL, // TODO
    .dequeue_block = NULL, // TODO

    .open_ev = NULL,  // TODO
    .close_ev = NULL, // TODO
    .read_ev = NULL,  // TODO
};

const struct iio_backend iio_xml_backend = {
    .api_version = IIO_BACKEND_API_V1,
    .name = "xml",
    .uri_prefix = "xml:",
    .ops = &xml_ops,
};

static int parse_context_attr(struct iio_context *ctx, xmlNode *n) {
  xmlAttr *attr;
  const char *name = NULL, *value = NULL;

  for (attr = n->properties; attr; attr = attr->next) {
    if (!strcmp((const char *)attr->name, "name")) {
      name = (const char *)attr->children->content;
    } else if (!strcmp((const char *)attr->name, "value")) {
      value = (const char *)attr->children->content;
    }
  }

  if (!name || !value)
    return -EINVAL;
  else
    return iio_context_add_attr(ctx, name, value);
}

static int iio_populate_xml_context_helper(struct iio_context *ctx,
                                           xmlNode *root) {
  xmlNode *n;
  int err;

  for (n = root->children; n; n = n->next) {
    if (!strcmp((char *)n->name, "context-attribute")) {
      err = parse_context_attr(ctx, n);
      if (err)
        return err;

      continue;
    } else if (strcmp((char *)n->name, "device")) {
      if (strcmp((char *)n->name, "text"))
        ctx_dbg(ctx,
                "Unknown children \'%s\' in "
                "<context>\n",
                n->name);
      continue;
    }

    err = create_device(ctx, n);
    if (err) {
      ctx_perror(ctx, err, "Unable to create device");
      return err;
    }
  }

  return 0;
}

static struct iio_context *
iio_create_xml_context_helper(const struct iio_context_params *params,
                              xmlDoc *doc) {
  const char *description = NULL, *git_tag = NULL, *content;
  struct iio_context *ctx;
  long major = 0, minor = 0;
  xmlNode *root;
  xmlAttr *attr;
  xmlChar *xmlbuff;
  int buffersize;
  char *end;
  int err;

  root = xmlDocGetRootElement(doc);
  if (strcmp((char *)root->name, "context")) {
    prm_err(params, "Unrecognized XML file\n");
    return iio_ptr(-EINVAL);
  }

  for (attr = root->properties; attr; attr = attr->next) {
    content = (const char *)attr->children->content;

    if (!strcmp((char *)attr->name, "description")) {
      description = content;
    } else if (!strcmp((char *)attr->name, "version-major")) {
      errno = 0;
      major = strtol(content, &end, 10);
      if (*end != '\0' || errno == ERANGE)
        prm_warn(params, "invalid format for major version\n");
    } else if (!strcmp((char *)attr->name, "version-minor")) {
      errno = 0;
      minor = strtol(content, &end, 10);
      if (*end != '\0' || errno == ERANGE)
        prm_warn(params, "invalid format for minor version\n");
    } else if (!strcmp((char *)attr->name, "version-git")) {
      git_tag = content;
    } else if (strcmp((char *)attr->name, "name")) {
      prm_dbg(params, "Unknown parameter \'%s\' in <context>\n", content);
    }
  }

  ctx = iio_context_create_from_backend(params, &iio_xml_backend, description,
                                        major, minor, git_tag);
  err = iio_err(ctx);
  if (err) {
    prm_err(params, "Unable to allocate memory for context\n");
    return iio_ptr(err);
  }

  err = iio_populate_xml_context_helper(ctx, root);
  if (err) {
    iio_context_destroy(ctx);
    return iio_ptr(err);
  }

  xmlDocDumpFormatMemory(doc, &xmlbuff, &buffersize, 1);
  ctx->xml_raw = (char *)malloc(sizeof(char) * buffersize);
  strcpy(ctx->xml_raw, (char *)xmlbuff);
  xmlFree(xmlbuff);

  return ctx;
}

static struct iio_context *
xml_create_context(const struct iio_context_params *params, const char *arg) {
  struct iio_context *ctx;
  xmlDoc *doc;

  LIBXML_TEST_VERSION;

  if (!strncmp(arg, XML_HEADER, sizeof(XML_HEADER) - 1)) {
    doc = xmlReadMemory(arg, (int)strlen(arg), NULL, NULL, XML_PARSE_DTDVALID);
  } else {
    doc = xmlReadFile(arg, NULL, XML_PARSE_DTDVALID);
  }

  if (!doc) {
    prm_err(params, "Unable to parse XML file\n");
    return iio_ptr(-EINVAL);
  }

  ctx = iio_create_xml_context_helper(params, doc);
  xmlFreeDoc(doc);

  return ctx;
}

void libiio_cleanup_xml_backend(void) {
  /*
   * This function will be called only when the libiio library is
   * unloaded (e.g. when the program exits).
   *
   * Cleanup libxml2 so that memory analyzer tools like Valgrind won't
   * detect a memory leak.
   */
  xmlCleanupParser();
  xmlMemoryDump();
}
