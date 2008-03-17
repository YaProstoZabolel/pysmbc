/* -*- Mode: C; c-file-style: "gnu" -*-
 * pysmbc - Python bindings for libsmbclient
 * Copyright (C) 2002, 2005, 2006, 2007, 2008  Tim Waugh <twaugh@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
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

#include <string.h>
#include "smbcmodule.h"
#include "context.h"
#include "dir.h"

Context *current_context;

static void
auth_fn (const char *server, const char *share,
	 char *workgroup, int wgmaxlen,
	 char *username, int unmaxlen,
	 char *password, int pwmaxlen)
{
  PyObject *args;
  PyObject *kwds;
  PyObject *result;
  const char *use_workgroup, *use_username, *use_password;

  debugprintf ("-> auth_fn (server=%s, share=%s)\n",
	       server ? server : "",
	       share ? share : "");

  if (current_context->auth_fn == NULL)
    {
      debugprintf ("<- auth_fn (), no callback\n");
      return;
    }

  if (!server || !*server)
    {
      debugprintf ("<- auth_fn(), no server\n");
      return;
    }

  args = Py_BuildValue ("(sssss)", server, share, workgroup,
			username, password);
  kwds = PyDict_New ();

  result = PyObject_Call (current_context->auth_fn, args, kwds);
  Py_DECREF (args);
  Py_DECREF (kwds);
  if (result == NULL)
    {
      debugprintf ("<- auth_fn(), failed callback\n");
      return;
    }

  if (!PyArg_ParseTuple (result, "sss",
			 &use_workgroup,
			 &use_username,
			 &use_password))
    {
      debugprintf ("<- auth_fn(), incorrect callback result\n");
      return;
    }

  strncpy (workgroup, use_workgroup, wgmaxlen);
  strncpy (username, use_username, unmaxlen);
  strncpy (password, use_password, pwmaxlen);
  debugprintf ("<- auth_fn(), got callback result\n");
}

static int
add_cached_srv_fn (SMBCCTX *c, SMBCSRV *srv,
		   const char *server, const char *share,
		   const char *workgroup, const char *username)
{
  return 0;
}

static SMBCSRV *
get_cached_srv_fn (SMBCCTX *c, const char *server, const char *share,
		   const char *workgroup, const char *username)
{
  return NULL;
}

static int
remove_cached_srv_fn (SMBCCTX *c, SMBCSRV *srv)
{
  return 0;
}

static int
purge_cached_fn (SMBCCTX *c)
{
  return 0;
}

/////////////
// Context //
/////////////

static PyObject *
Context_new (PyTypeObject *type, PyObject *args, PyObject *kwds)
{
  Context *self;
  self = (Context *) type->tp_alloc (type, 0);
  if (self != NULL)
    self->context = NULL;

  return (PyObject *) self;
}

static int
Context_init (Context *self, PyObject *args, PyObject *kwds)
{
  PyObject *auth = NULL;
  int debug = -1;
  unsigned int flags = 0;
  SMBCCTX *ctx;
  static char *kwlist[] = 
    {
      "auth_fn",
      "debug",
      "flags",
      NULL
    };

  if (!PyArg_ParseTupleAndKeywords (args, kwds, "|Oii", kwlist,
				    &auth, &debug, &flags))
    return -1;

  if (auth)
    {
      if (!PyCallable_Check (auth))
	{
	  PyErr_SetString (PyExc_TypeError, "auth_fn must be callable");
	  return -1;
	}

      Py_XINCREF (auth);
      self->auth_fn = auth;
    }

  debugprintf ("-> Context_init ()\n");

  ctx = smbc_new_context ();
  if (ctx == NULL)
    {
      PyErr_SetFromErrno (PyExc_RuntimeError);
      debugprintf ("<- Context_init() EXCEPTION\n");
      return -1;
    }

  if (debug != -1)
    ctx->debug = debug;

  ctx->flags |= flags;
  ctx->callbacks.auth_fn = auth_fn;
  ctx->callbacks.add_cached_srv_fn = add_cached_srv_fn;
  ctx->callbacks.get_cached_srv_fn = get_cached_srv_fn;
  ctx->callbacks.remove_cached_srv_fn = remove_cached_srv_fn;
  ctx->callbacks.purge_cached_fn = purge_cached_fn;
  if (smbc_init_context (ctx) == NULL)
    {
      PyErr_SetFromErrno (PyExc_RuntimeError);
      smbc_free_context (ctx, 0);
      debugprintf ("<- Context_init() EXCEPTION\n");
      return -1;
    }

  self->context = ctx;
  debugprintf ("%p <- Context_init() = 0\n", self->context);
  return 0;
}

static void
Context_dealloc (Context *self)
{
  if (self->context)
    {
      debugprintf ("%p smbc_free_context()\n", self->context);
      smbc_free_context (self->context, 1);
    }

  self->ob_type->tp_free ((PyObject *) self);
}

static PyObject *
Context_opendir (Context *self, PyObject *args)
{
  PyObject *largs, *lkwlist;
  PyObject *uri;
  PyObject *dir;

  debugprintf ("%p -> Context_opendir()\n", self->context);
  if (!PyArg_ParseTuple (args, "O", &uri))
    {
      debugprintf ("%p <- Context_opendir() EXCEPTION\n", self->context);
      return NULL;
    }

  largs = Py_BuildValue ("()");
  lkwlist = PyDict_New ();
  PyDict_SetItemString (lkwlist, "context", (PyObject *) self);
  PyDict_SetItemString (lkwlist, "uri", uri);
  dir = PyType_GenericNew (&smbc_DirType, largs, lkwlist);
  if (smbc_DirType.tp_init (dir, largs, lkwlist) < 0)
    {
      smbc_DirType.tp_dealloc (dir);
      debugprintf ("%p <- Context_opendir() EXCEPTION\n", self->context);
      return NULL;
    }

  debugprintf ("%p <- Context_opendir() = Dir\n", self->context);
  return dir;
}

PyMethodDef Context_methods[] =
  {
    { "opendir",
      (PyCFunction) Context_opendir, METH_VARARGS,
      "opendir(uri) -> Dir\n\n"
      "@type uri: string\n"
      "@param uri: URI to open\n"
      "@return: a L{smbc.Dir} object for the URI" },

    { NULL } /* Sentinel */
  };

PyTypeObject smbc_ContextType =
  {
    PyObject_HEAD_INIT(NULL)
    0,                         /*ob_size*/
    "smbc.Context",            /*tp_name*/
    sizeof(Context),           /*tp_basicsize*/
    0,                         /*tp_itemsize*/
    (destructor)Context_dealloc, /*tp_dealloc*/
    0,                         /*tp_print*/
    0,                         /*tp_getattr*/
    0,                         /*tp_setattr*/
    0,                         /*tp_compare*/
    0,                         /*tp_repr*/
    0,                         /*tp_as_number*/
    0,                         /*tp_as_sequence*/
    0,                         /*tp_as_mapping*/
    0,                         /*tp_hash */
    0,                         /*tp_call*/
    0,                         /*tp_str*/
    0,                         /*tp_getattro*/
    0,                         /*tp_setattro*/
    0,                         /*tp_as_buffer*/
    Py_TPFLAGS_DEFAULT,        /*tp_flags*/
    "SMBC context\n"
    "============\n\n"

    "  A context for libsmbclient calls."
    "",                        /* tp_doc */
    0,                         /* tp_traverse */
    0,                         /* tp_clear */
    0,                         /* tp_richcompare */
    0,                         /* tp_weaklistoffset */
    0,                         /* tp_iter */
    0,                         /* tp_iternext */
    Context_methods,           /* tp_methods */
    0,                         /* tp_members */
    0,                         /* tp_getset */
    0,                         /* tp_base */
    0,                         /* tp_dict */
    0,                         /* tp_descr_get */
    0,                         /* tp_descr_set */
    0,                         /* tp_dictoffset */
    (initproc)Context_init,    /* tp_init */
    0,                         /* tp_alloc */
    Context_new,               /* tp_new */
  };