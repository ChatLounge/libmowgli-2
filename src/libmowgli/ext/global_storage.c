/*
 * libmowgli: A collection of useful routines for programming.
 * global_storage.c: Simple key->value global storage tool.
 *
 * Copyright (c) 2007, 2011 William Pitcock <nenolod@dereferenced.org>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice is present in all copies.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "mowgli.h"

static mowgli_patricia_t *mowgli_global_storage_dict = NULL;
static mowgli_mutex_t mowgli_global_storage_lock;

static void
_storage_key_canon(char *key)
{ }

void
mowgli_global_storage_bootstrap(void)
{
	mowgli_global_storage_dict = mowgli_patricia_create(_storage_key_canon);

	mowgli_mutex_init(&mowgli_global_storage_lock);
}

void *
mowgli_global_storage_get(char *name)
{
	void *ret;

	mowgli_mutex_lock(&mowgli_global_storage_lock);
	ret = mowgli_patricia_retrieve(mowgli_global_storage_dict, name);
	mowgli_mutex_unlock(&mowgli_global_storage_lock);

	return ret;
}

void
mowgli_global_storage_put(char *name, void *value)
{
	mowgli_mutex_lock(&mowgli_global_storage_lock);
	mowgli_patricia_add(mowgli_global_storage_dict, name, value);
	mowgli_mutex_unlock(&mowgli_global_storage_lock);
}

void
mowgli_global_storage_free(char *name)
{
	mowgli_mutex_lock(&mowgli_global_storage_lock);
	mowgli_patricia_delete(mowgli_global_storage_dict, name);
	mowgli_mutex_unlock(&mowgli_global_storage_lock);
}
