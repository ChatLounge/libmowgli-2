/*
 * libmowgli: A collection of useful routines for programming.
 * vio_openssl.c: OpenSSL routines built on VIO
 *
 * Copyright (c) 2012 Elizabeth J. Myers. All rights reserved.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
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

/* Note these routines are just defaults for clients -- if you have more
 * specific needs, you should write your own implementation (that is the
 * whole point of vio)
 */

#ifdef HAVE_OPENSSL

typedef struct {
	SSL *ssl_handle;
	SSL_CTX *ssl_context;
} mowgli_ssl_connection_t;

static int mowgli_vio_openssl_connect(mowgli_vio_t *vio);
static int mowgli_vio_openssl_ssl_handshake(mowgli_vio_t *vio, mowgli_ssl_connection_t *connection);
static int mowgli_vio_openssl_read(mowgli_vio_t *vio, void *buffer, size_t len);
static int mowgli_vio_openssl_write(mowgli_vio_t *vio, void *buffer, size_t len);
static int mowgli_vio_openssl_close(mowgli_vio_t *vio);

static mowgli_heap_t *ssl_heap = NULL;

static bool openssl_init = false;

int mowgli_vio_openssl_setssl(mowgli_vio_t *vio, void *attr)
{
	mowgli_ssl_connection_t *connection; 

	if (!ssl_heap)
		ssl_heap = mowgli_heap_create(sizeof(mowgli_ssl_connection_t), 64, BH_NOW);

	connection = mowgli_heap_alloc(ssl_heap);
	vio->privdata = connection;

	/* Change ops */
	mowgli_vio_set_op(vio, connect, mowgli_vio_openssl_connect);
	mowgli_vio_set_op(vio, read, mowgli_vio_openssl_read);
	mowgli_vio_set_op(vio, write, mowgli_vio_openssl_write);
	mowgli_vio_set_op(vio, close, mowgli_vio_openssl_close);

	/* SSL setup */
	if (!openssl_init)
	{
		openssl_init = true;
		SSL_load_error_strings();
		SSL_library_init();
	}

	return 0;
}

/* Returns void so they can be stubs */
void * mowgli_vio_openssl_getsslhandle(mowgli_vio_t *vio)
{
	mowgli_ssl_connection_t *connection = vio->privdata;
	return connection->ssl_handle;
}

void * mowgli_vio_openssl_getsslcontext(mowgli_vio_t *vio)
{
	mowgli_ssl_connection_t *connection = vio->privdata;
	return connection->ssl_context;
}

static int mowgli_vio_openssl_connect(mowgli_vio_t *vio)
{
	vio->error.op = MOWGLI_VIO_ERR_OP_CONNECT;
	mowgli_ssl_connection_t *connection = vio->privdata;

	if (connect(vio->fd, vio->addr, vio->addrlen) < 0)
	{
		if (!mowgli_eventloop_ignore_errno(errno))
		{
			MOWGLI_VIO_RETURN_ERRCODE(vio, strerror, errno);
		}
		else
		{
			mowgli_vio_setflag(vio, MOWGLI_VIO_FLAGS_ISCONNECTING, true);
			mowgli_vio_setflag(vio, MOWGLI_VIO_FLAGS_ISSSLCONNECTING, true);
			vio->error.op = MOWGLI_VIO_ERR_OP_NONE;
			return 0;
		}
	}

	/* Non-blocking socket, begin handshake */
	mowgli_vio_setflag(vio, MOWGLI_VIO_FLAGS_ISCONNECTING, false);
	return mowgli_vio_openssl_ssl_handshake(vio, connection);
}

static int mowgli_vio_openssl_ssl_handshake(mowgli_vio_t *vio, mowgli_ssl_connection_t *connection)
{
	int ret;

	vio->error.op = MOWGLI_VIO_ERR_OP_CONNECT;

	/* Good default; most stuff isn't using TLSv1 or above yet (unfortunately)
	 * The library will negotiate something better if supported.
	 */
	connection->ssl_context = SSL_CTX_new(SSLv3_client_method());
	if (connection->ssl_context == NULL)
		MOWGLI_VIO_RETURN_SSLERR_ERRCODE(vio, ERR_get_error())

	connection->ssl_handle = SSL_new(connection->ssl_context);
	if (connection->ssl_handle == NULL)
		MOWGLI_VIO_RETURN_SSLERR_ERRCODE(vio, ERR_get_error())
	
	if (!SSL_set_fd(connection->ssl_handle, vio->fd))
		MOWGLI_VIO_RETURN_SSLERR_ERRCODE(vio, ERR_get_error())

	/* XXX not what we want for blocking sockets if they're in use! */
	SSL_CTX_set_mode(connection->ssl_context, SSL_MODE_ENABLE_PARTIAL_WRITE);

	if ((ret = SSL_connect(connection->ssl_handle)) != 1)
	{
		MOWGLI_VIO_RETURN_SSLERR_ERRCODE(vio, SSL_get_error(connection->ssl_handle, ret))
	}

	/* Connected */
	mowgli_vio_setflag(vio, MOWGLI_VIO_FLAGS_ISSSLCONNECTING, false);

	vio->error.op = MOWGLI_VIO_ERR_OP_NONE;
	return 0;
}

static int mowgli_vio_openssl_read(mowgli_vio_t *vio, void *buffer, size_t len)
{
	int ret;
	mowgli_ssl_connection_t *connection = vio->privdata;

	/* Establish SSL connection */
	if (mowgli_vio_hasflag(vio, MOWGLI_VIO_FLAGS_ISSSLCONNECTING))
	{
		return mowgli_vio_openssl_ssl_handshake(vio, connection);
	}

	/* We're connected */
	mowgli_vio_setflag(vio, MOWGLI_VIO_FLAGS_ISCONNECTING, false);

	vio->error.op = MOWGLI_VIO_ERR_OP_READ;

	if ((ret = (int)SSL_read(connection->ssl_handle, buffer, len)) < 0)
	{
		int err = SSL_get_error(connection->ssl_handle, ret);

		if (err != SSL_ERROR_WANT_READ)
		{
			MOWGLI_VIO_RETURN_SSLERR_ERRCODE(vio, err)
		}

		if (ret == 0)
		{
			vio->error.type = MOWGLI_VIO_ERR_REMOTE_HANGUP;
			mowgli_strlcpy(vio->error.string, "Remote host closed the socket", sizeof(vio->error.string));

			MOWGLI_VIO_SET_CLOSED(vio);

			return mowgli_vio_error(vio);
		}

		return 0;
	}

	vio->error.op = MOWGLI_VIO_ERR_OP_NONE;
	return ret;
}

static int mowgli_vio_openssl_write(mowgli_vio_t *vio, void *buffer, size_t len)
{
	int ret;
	mowgli_ssl_connection_t *connection = vio->privdata;

	/* Establish SSL connection */
        if (mowgli_vio_hasflag(vio, MOWGLI_VIO_FLAGS_ISSSLCONNECTING))
        {
                return mowgli_vio_openssl_ssl_handshake(vio, connection);
        }

	/* We're connected */
	mowgli_vio_setflag(vio, MOWGLI_VIO_FLAGS_ISCONNECTING, false);

	vio->error.op = MOWGLI_VIO_ERR_OP_WRITE;

	if ((ret = (int)SSL_write(connection->ssl_handle, buffer, len)) == -1)
	{
		int err = SSL_get_error(connection->ssl_handle, ret);

		if (err != SSL_ERROR_WANT_WRITE)
		{
			MOWGLI_VIO_RETURN_SSLERR_ERRCODE(vio, err)
		}
		else
		{
			return 0;
		}
	}

	vio->error.op = MOWGLI_VIO_ERR_OP_NONE;
	return ret;
}

static int mowgli_vio_openssl_close(mowgli_vio_t *vio)
{
	mowgli_ssl_connection_t *connection = vio->privdata;

	SSL_shutdown(connection->ssl_handle);
	SSL_free(connection->ssl_handle);
	SSL_CTX_free(connection->ssl_context);

	mowgli_heap_free(ssl_heap, connection);

	vio->flags &= ~MOWGLI_VIO_FLAGS_ISCONNECTING;
	vio->flags |= MOWGLI_VIO_FLAGS_ISCLOSED;

	close(vio->fd);
	return 0;
}

#else

int mowgli_vio_openssl_setssl(mowgli_vio_t *vio)
{
	mowgli_log("OpenSSL requested on a VIO object, but mowgli was built without OpenSSL support...");
	return -1;
}

void * mowgli_vio_openssl_getsslhandle(mowgli_vio_t *vio)
{
	mowgli_log("Cannot get VIO SSL handle as libmowgli was built without OpenSSL support");
	return NULL;
}

void * mowgli_vio_openssl_getsslcontext(mowgli_vio_t *vio)
{
	mowgli_log("Cannot get VIO SSL context as libmowgli was built without OpenSSL support");
	return NULL;
}

#endif

