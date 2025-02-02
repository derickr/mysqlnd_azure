/*
  +----------------------------------------------------------------------+
  | PHP Version 7                                                        |
  +----------------------------------------------------------------------+
  | Copyright (c) 2006-2018 The PHP Group                                |
  +----------------------------------------------------------------------+
  | This source file is subject to version 3.01 of the PHP license,      |
  | that is bundled with this package in the file LICENSE, and is        |
  | available through the world-wide-web at the following url:           |
  | http://www.php.net/license/3_01.txt                                  |
  | If you did not receive a copy of the PHP license and are unable to   |
  | obtain it through the world-wide-web, please send a note to          |
  | license@php.net so we can mail you a copy immediately.               |
  +----------------------------------------------------------------------+
  | Authors: Qianqian Bu <qianqian.bu@microsoft.com>                     |
  +----------------------------------------------------------------------+
*/


#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "php_mysqlnd_azure.h"
#include "mysqlnd_azure.h"
#include "ext/standard/info.h"
#include "ext/mysqlnd/mysqlnd_ext_plugin.h"
#include "ext/mysqlnd/mysqlnd_structs.h"
#include "ext/mysqlnd/mysqlnd_statistics.h"
#include "ext/mysqlnd/mysqlnd_connection.h"

unsigned int mysqlnd_azure_plugin_id;
struct st_mysqlnd_conn_data_methods org_conn_d_m;
struct st_mysqlnd_conn_data_methods* conn_d_m;
struct st_mysqlnd_conn_methods org_conn_m;
struct st_mysqlnd_conn_methods* conn_m;

/* {{{ mysqlnd_conn_data::dtor */
/*  to free the extra data is_using_redirect*/
static void
MYSQLND_METHOD_PRIVATE(mysqlnd_azure_data, dtor)(MYSQLND_CONN_DATA * conn)
{
	DBG_ENTER("mysqlnd_azure_data::dtor");
	MYSQLND_AZURE_CONN_DATA** props = (MYSQLND_AZURE_CONN_DATA**)mysqlnd_plugin_get_plugin_connection_data_data(conn, mysqlnd_azure_plugin_id);

	if (props && *props) {
		mnd_pefree((*props), conn->persistent);
		*props = NULL;
	}

	DBG_RETURN(org_conn_d_m.dtor(conn));
}
/* }}} */

/* {{{ set_redirect_client_options */
static enum_func_status
set_redirect_client_options(MYSQLND_CONN_DATA * const conn, MYSQLND_CONN_DATA * const redirectConn)
{
	//TODO: the fields copies here are from list that is handled in mysqlnd_conn_data::set_client_option, may not compelete, and may need update when mysqlnd_conn_data::set_client_option updates
	DBG_ENTER("mysqlnd_azure_data::set_redirect_client_options Copy client options for redirection connection");
	enum_func_status ret = FAIL;

	redirectConn->client_api_capabilities = conn->client_api_capabilities;

	redirectConn->vio->data->ssl = conn->vio->data->ssl;
	redirectConn->vio->data->options.timeout_read = conn->vio->data->options.timeout_read;
	redirectConn->vio->data->options.timeout_write = conn->vio->data->options.timeout_write;
	redirectConn->vio->data->options.timeout_connect = conn->vio->data->options.timeout_connect;
	redirectConn->vio->data->options.ssl_verify_peer = conn->vio->data->options.ssl_verify_peer;

	ret = redirectConn->vio->data->m.set_client_option(redirectConn->vio, MYSQLND_OPT_SSL_KEY, conn->vio->data->options.ssl_key);
	if (ret == FAIL) goto copyFailed;

	ret = redirectConn->vio->data->m.set_client_option(redirectConn->vio, MYSQLND_OPT_SSL_CERT, conn->vio->data->options.ssl_cert);
	if (ret == FAIL) goto copyFailed;

	ret = redirectConn->vio->data->m.set_client_option(redirectConn->vio, MYSQLND_OPT_SSL_CA, conn->vio->data->options.ssl_ca);
	if (ret == FAIL) goto copyFailed;

	ret = redirectConn->vio->data->m.set_client_option(redirectConn->vio, MYSQLND_OPT_SSL_CAPATH, conn->vio->data->options.ssl_capath);
	if (ret == FAIL) goto copyFailed;

	ret = redirectConn->vio->data->m.set_client_option(redirectConn->vio, MYSQLND_OPT_SSL_CIPHER, conn->vio->data->options.ssl_cipher);
	if (ret == FAIL) goto copyFailed;

	ret = redirectConn->vio->data->m.set_client_option(redirectConn->vio, MYSQLND_OPT_NET_READ_BUFFER_SIZE, (const char *)&conn->vio->data->options.net_read_buffer_size);
	if (ret == FAIL) goto copyFailed;

	ret = redirectConn->protocol_frame_codec->data->m.set_client_option(redirectConn->protocol_frame_codec, MYSQLND_OPT_NET_CMD_BUFFER_SIZE, (const char *)&conn->protocol_frame_codec->cmd_buffer.length);
	if (ret == FAIL) goto copyFailed;

	//MYSQL_OPT_COMPRESS
	if (conn->protocol_frame_codec->data->flags & MYSQLND_PROTOCOL_FLAG_USE_COMPRESSION) {
		redirectConn->protocol_frame_codec->data->flags |= MYSQLND_PROTOCOL_FLAG_USE_COMPRESSION;
	}
	else {
		redirectConn->protocol_frame_codec->data->flags &= ~MYSQLND_PROTOCOL_FLAG_USE_COMPRESSION;
	}

	ret = redirectConn->protocol_frame_codec->data->m.set_client_option(redirectConn->protocol_frame_codec, MYSQL_SERVER_PUBLIC_KEY, conn->protocol_frame_codec->data->sha256_server_public_key);
	if (ret == FAIL) goto copyFailed;

#ifdef MYSQLND_STRING_TO_INT_CONVERSION
	redirectConn->options->int_and_float_native = conn->options->int_and_float_native;
#endif

	redirectConn->options->flags = conn->options->flags;

	//MYSQL_INIT_COMMAND:
	{
		if (redirectConn->options->num_commands) {
			unsigned int i;
			for (i = 0; i < redirectConn->options->num_commands; i++) {
				/* allocated with pestrdup */
				mnd_pefree(redirectConn->options->init_commands[i], redirectConn->persistent);
			}
			mnd_pefree(redirectConn->options->init_commands, redirectConn->persistent);
			redirectConn->options->init_commands = NULL;
		}

		if (conn->options->num_commands)
		{
			char ** new_init_commands;
			new_init_commands = mnd_perealloc(redirectConn->options->init_commands, sizeof(char *) * (conn->options->num_commands), conn->persistent);
			if (!new_init_commands) {
				SET_OOM_ERROR(redirectConn->error_info);
				goto copyFailed;
			}
			redirectConn->options->init_commands = new_init_commands;
			unsigned int i;
			char * new_command;
			for (i = 0; i < conn->options->num_commands; i++) {
				new_command = mnd_pestrdup(conn->options->init_commands[i], conn->persistent);
				if (!new_command) {
					SET_OOM_ERROR(redirectConn->error_info);
					goto copyFailed;
				}
				redirectConn->options->init_commands[i] = new_command;
				++redirectConn->options->num_commands;
			}
		}
	}

	if (conn->options->charset_name != NULL) {
		ret = redirectConn->m->set_client_option(redirectConn, MYSQL_SET_CHARSET_NAME, conn->options->charset_name);
		if (ret == FAIL) goto copyFailed;
	}

	redirectConn->options->protocol = conn->options->protocol;
	redirectConn->options->max_allowed_packet = conn->options->max_allowed_packet;

	ret = redirectConn->m->set_client_option(redirectConn, MYSQLND_OPT_AUTH_PROTOCOL, conn->options->auth_protocol);
	if (ret == FAIL) goto copyFailed;

	//MYSQL_OPT_CONNECT_ATTR_xx
	{
		if (redirectConn->options->connect_attr) {
			zend_hash_destroy(redirectConn->options->connect_attr);
			mnd_pefree(redirectConn->options->connect_attr, redirectConn->persistent);
			redirectConn->options->connect_attr = NULL;
		}
		zend_string * key;
		zval * entry_value;
		ZEND_HASH_FOREACH_STR_KEY_VAL(conn->options->connect_attr, key, entry_value) {
			ret = redirectConn->m->set_client_option_2d(redirectConn, MYSQL_OPT_CONNECT_ATTR_ADD, ZSTR_VAL(key), Z_STRVAL_P(entry_value));
			if (ret == FAIL) goto copyFailed;
		} ZEND_HASH_FOREACH_END();
	}

	DBG_RETURN(ret);

copyFailed:
	DBG_RETURN(FAIL);
}
/* }}} */

/* {{{ mysqlnd_azure_data::connect */
MYSQLND_METHOD(mysqlnd_azure_data, connect)(MYSQLND_CONN_DATA ** pconn,
						MYSQLND_CSTRING hostname,
						MYSQLND_CSTRING username,
						MYSQLND_CSTRING password,
						MYSQLND_CSTRING database,
						unsigned int port,
						MYSQLND_CSTRING socket_or_pipe,
						unsigned int mysql_flags
					)
{
	MYSQLND_CONN_DATA * conn = *pconn;

	const size_t this_func = STRUCT_OFFSET(MYSQLND_CLASS_METHODS_TYPE(mysqlnd_conn_data), connect);
	zend_bool unix_socket = FALSE;
	zend_bool named_pipe = FALSE;
	zend_bool reconnect = FALSE;
	zend_bool saved_compression = FALSE;
	zend_bool local_tx_started = FALSE;
	MYSQLND_PFC * pfc = conn->protocol_frame_codec;
	MYSQLND_STRING transport = { NULL, 0 };

	DBG_ENTER("mysqlnd_conn_data::connect");
	DBG_INF_FMT("conn=%p", conn);

	if (PASS != conn->m->local_tx_start(conn, this_func)) {
		goto err;
	}
	local_tx_started = TRUE;

	SET_EMPTY_ERROR(conn->error_info);
	UPSERT_STATUS_SET_AFFECTED_ROWS_TO_ERROR(conn->upsert_status);

	DBG_INF_FMT("host=%s user=%s db=%s port=%u flags=%u persistent=%u state=%u",
				hostname.s?hostname.s:"", username.s?username.s:"", database.s?database.s:"", port, mysql_flags,
				conn? conn->persistent:0, conn? (int)GET_CONNECTION_STATE(&conn->state):-1);

	if (GET_CONNECTION_STATE(&conn->state) > CONN_ALLOCED) {
		DBG_INF("Connecting on a connected handle.");

		if (GET_CONNECTION_STATE(&conn->state) < CONN_QUIT_SENT) {
			MYSQLND_INC_CONN_STATISTIC(conn->stats, STAT_CLOSE_IMPLICIT);
			reconnect = TRUE;
			conn->m->send_close(conn);
		}

		conn->m->free_contents(conn);
		/* Now reconnect using the same handle */
		if (pfc->data->compressed) {
			/*
			  we need to save the state. As we will re-connect, pfc->compressed should be off, or
			  we will look for a compression header as part of the greet message, but there will
			  be none.
			*/
			saved_compression = TRUE;
			pfc->data->compressed = FALSE;
		}
		if (pfc->data->ssl) {
			pfc->data->ssl = FALSE;
		}
	} else {
		unsigned int max_allowed_size = MYSQLND_ASSEMBLED_PACKET_MAX_SIZE;
		conn->m->set_client_option(conn, MYSQLND_OPT_MAX_ALLOWED_PACKET, (char *)&max_allowed_size);
	}

	if (!hostname.s || !hostname.s[0]) {
		hostname.s = "localhost";
		hostname.l = strlen(hostname.s);
	}
	if (!username.s) {
		DBG_INF_FMT("no user given, using empty string");
		username.s = "";
		username.l = 0;
	}
	if (!password.s) {
		DBG_INF_FMT("no password given, using empty string");
		password.s = "";
		password.l = 0;
	}
	if (!database.s) {
		DBG_INF_FMT("no db given, using empty string");
		database.s = "";
		database.l = 0;
	} else {
		mysql_flags |= CLIENT_CONNECT_WITH_DB;
	}

	transport = conn->m->get_scheme(conn, hostname, &socket_or_pipe, port, &unix_socket, &named_pipe);

	mysql_flags = conn->m->get_updated_connect_flags(conn, mysql_flags);

	{
		const MYSQLND_CSTRING scheme = { transport.s, transport.l };
		if (FAIL == conn->m->connect_handshake(conn, &scheme, &username, &password, &database, mysql_flags)) {
			goto err;
		}
	}

	/*start of Azure Redirection logic*/
	//Redirect before run init_command
	{
		SET_CONNECTION_STATE(&conn->state, CONN_READY); //set ready status so the connection can be closed correctly later if redirect succeeds

		MYSQLND_AZURE_CONN_DATA** pdata = mysqlnd_azure_get_is_using_redirect(conn);

		if (!(*pdata)->is_using_redirect && conn->last_message.l > 27 && (strncmp((char*)conn->last_message.s, "Location:", strlen("Location:")) == 0) && ((strstr((char*)conn->last_message.s, "mysql://")) != NULL)) { //there is a redirection connection info we can take use of
			/**
			* Get redirected server information contained in OK packet.
			* Redirection string somehow look like:
			* Location: mysql://redirectedHostName:redirectedPort/user=redirectedUser
			* the minimal len is 27 bytes
			*/
			DBG_ENTER("[redirect] mysqlnd_azure_data::connect::redirect");
			char redirect_host[MAX_REDIRECT_HOST_LEN] = { 0 };
			char redirect_user[MAX_REDIRECT_USER_LEN] = { 0 };
			unsigned int ui_redirect_port = 0;
			char  redirect_port[8] = { 0 };

			int redirect_total_len = conn->last_message.l;
			unsigned char *cur_pos = conn->last_message.s;
			char *p1 = strstr((char*)cur_pos, "//");
			char *p2 = strstr(p1, ":");
			char *p3 = strstr((char*)cur_pos, "user=");

			int redirect_host_len = p2 - p1 - 2;
			int redirect_port_len = p3 - p2 - 2;
			int redirect_user_len = redirect_total_len - (p3 + 5 - ((char*)cur_pos));
			if (redirect_host_len <= 0 || redirect_port_len <= 0 || redirect_user_len <= 0) {
				redirect_host_len = redirect_port_len = redirect_user_len = 0;
			}
			else {
				memcpy(redirect_host, p1 + 2, redirect_host_len);
				memcpy(redirect_user, p3 + 5, redirect_user_len);
				memcpy(redirect_port, p2 + 1, redirect_port_len);
				ui_redirect_port = atoi(redirect_port);
			}

			if (redirect_host_len > 0 && redirect_user_len > 0 && redirect_port_len > 0
				&& (strcmp(redirect_host, hostname.s) || strcmp(redirect_user, username.s) || ui_redirect_port != port)) { //currently used conn is not redirected connection
				enum_func_status ret = FAIL;
				MYSQLND* redirect_conneHandle = mysqlnd_init(MYSQLND_CLIENT_KNOWS_RSET_COPY_DATA, conn->persistent); //init MYSQLND but only need only MYSQLND_CONN_DATA here
				MYSQLND_CONN_DATA* redirect_conn = redirect_conneHandle->data;
				redirect_conneHandle->data = NULL;
				mnd_pefree(redirect_conneHandle, redirect_conneHandle->persistent);
				redirect_conneHandle = NULL;
				
				DBG_INF_FMT("[redirect]: redirect host=%s user=%s port=%s ", redirect_host, redirect_user, redirect_port);

				ret = set_redirect_client_options(conn, redirect_conn);
				if (ret == FAIL) { //init redirect_conn failed
					redirect_conn->m->dtor(redirect_conn);
					DBG_ENTER("[redirect]: mysql redirect fails to copy MYSQLND_CONN_DATA, use original connection information!");
				}
				else { //init redirect_conn succeeded, use this conn to start a new connection and handshake
					MYSQLND_CSTRING redirect_hostname = { redirect_host, strlen(redirect_host) };
					MYSQLND_CSTRING redirect_username = { redirect_user, strlen(redirect_user) };
					MYSQLND_STRING redirect_transport = redirect_conn->m->get_scheme(redirect_conn, redirect_hostname, &socket_or_pipe, ui_redirect_port, &unix_socket, &named_pipe);
					const MYSQLND_CSTRING redirect_scheme = { redirect_transport.s, redirect_transport.l };

					mysqlnd_azure_set_is_using_redirect(redirect_conn, 1);

					enum_func_status redirectState = redirect_conn->m->connect_handshake(redirect_conn, &redirect_scheme, &redirect_username, &password, &database, mysql_flags);

					if (redirectState == FAIL) { //handshake with new redirection MYSQLND_CONN_DATA failed, release resource and use original connection
						redirect_conn->m->dtor(redirect_conn);
						if (redirect_transport.s) {
							mnd_sprintf_free(redirect_transport.s);
							redirect_transport.s = NULL;
						}
						DBG_ENTER("[redirect]: mysql redirect handshake fails, use original connection information!");
					}
					else { //handshake with redirect_conn succeeded, close and release resource of original conn and replace it with redirect_conn

						//add the redirect info into cache table
						mysqlnd_azure_add_redirect_cache(conn, username.s, hostname.s, port, redirect_username.s, redirect_hostname.s, ui_redirect_port);

						conn->m->send_close(conn);
						conn->m->dtor(conn);
						pfc = NULL;
						//upate conn, transport,  pconn for later user
						conn = redirect_conn;
						if (transport.s) {
							mnd_sprintf_free(transport.s);
							transport.s = NULL;
						}
						transport = redirect_transport;
						*pconn = redirect_conn; //use new conn outside for caller

						///upate host, user, pfc for later user
						hostname = redirect_hostname;
						username = redirect_username;
						port = ui_redirect_port;
						pfc = redirect_conn->protocol_frame_codec;

						DBG_ENTER("[redirect]: mysql redirect handshake succeeded.");
					}
				}
			}
			else {
				DBG_ENTER("[redirect]: redirection info are equal to origin, no need to redirect");
			}
		}
		else {
			DBG_ENTER("[redirect]: already use redirection info or can not find redirection location in ok packet");
		}
	}
	/*end of Azure Redirection Logic*/

	{
		SET_CONNECTION_STATE(&conn->state, CONN_READY);

		if (saved_compression) {
			pfc->data->compressed = TRUE;
		}
		/*
		  If a connect on a existing handle is performed and mysql_flags is
		  passed which doesn't CLIENT_COMPRESS, then we need to overwrite the value
		  which we set based on saved_compression.
		*/
		pfc->data->compressed = mysql_flags & CLIENT_COMPRESS? TRUE:FALSE;


		conn->scheme.s = mnd_pestrndup(transport.s, transport.l, conn->persistent);
		conn->scheme.l = transport.l;
		if (transport.s) {
			mnd_sprintf_free(transport.s);
			transport.s = NULL;
		}

		if (!conn->scheme.s) {
			goto err; /* OOM */
		}

		conn->username.l		= username.l;
		conn->username.s		= mnd_pestrndup(username.s, conn->username.l, conn->persistent);
		conn->password.l		= password.l;
		conn->password.s		= mnd_pestrndup(password.s, conn->password.l, conn->persistent);
		conn->port				= port;
		conn->connect_or_select_db.l = database.l;
		conn->connect_or_select_db.s = mnd_pestrndup(database.s, conn->connect_or_select_db.l, conn->persistent);

		if (!conn->username.s || !conn->password.s|| !conn->connect_or_select_db.s) {
			SET_OOM_ERROR(conn->error_info);
			goto err; /* OOM */
		}

		if (!unix_socket && !named_pipe) {
			conn->hostname.s = mnd_pestrndup(hostname.s, hostname.l, conn->persistent);
			if (!conn->hostname.s) {
				SET_OOM_ERROR(conn->error_info);
				goto err; /* OOM */
			}
			conn->hostname.l = hostname.l;
			{
				char *p;
				mnd_sprintf(&p, 0, "%s via TCP/IP", conn->hostname.s);
				if (!p) {
					SET_OOM_ERROR(conn->error_info);
					goto err; /* OOM */
				}
				conn->host_info = mnd_pestrdup(p, conn->persistent);
				mnd_sprintf_free(p);
				if (!conn->host_info) {
					SET_OOM_ERROR(conn->error_info);
					goto err; /* OOM */
				}
			}
		} else {
			conn->unix_socket.s = mnd_pestrdup(socket_or_pipe.s, conn->persistent);
			if (unix_socket) {
				conn->host_info = mnd_pestrdup("Localhost via UNIX socket", conn->persistent);
			} else if (named_pipe) {
				char *p;
				mnd_sprintf(&p, 0, "%s via named pipe", conn->unix_socket.s);
				if (!p) {
					SET_OOM_ERROR(conn->error_info);
					goto err; /* OOM */
				}
				conn->host_info =  mnd_pestrdup(p, conn->persistent);
				mnd_sprintf_free(p);
				if (!conn->host_info) {
					SET_OOM_ERROR(conn->error_info);
					goto err; /* OOM */
				}
			} else {
				php_error_docref(NULL, E_WARNING, "Impossible. Should be either socket or a pipe. Report a bug!");
			}
			if (!conn->unix_socket.s || !conn->host_info) {
				SET_OOM_ERROR(conn->error_info);
				goto err; /* OOM */
			}
			conn->unix_socket.l = strlen(conn->unix_socket.s);
		}

		SET_EMPTY_ERROR(conn->error_info);

		mysqlnd_local_infile_default(conn);

		if (FAIL == conn->m->execute_init_commands(conn)) {
			goto err;
		}

		MYSQLND_INC_CONN_STATISTIC_W_VALUE2(conn->stats, STAT_CONNECT_SUCCESS, 1, STAT_OPENED_CONNECTIONS, 1);
		if (reconnect) {
			MYSQLND_INC_GLOBAL_STATISTIC(STAT_RECONNECT);
		}
		if (conn->persistent) {
			MYSQLND_INC_CONN_STATISTIC_W_VALUE2(conn->stats, STAT_PCONNECT_SUCCESS, 1, STAT_OPENED_PERSISTENT_CONNECTIONS, 1);
		}

		DBG_INF_FMT("connection_id=%llu", conn->thread_id);

		conn->m->local_tx_end(conn, this_func, PASS);
		DBG_RETURN(PASS);
	}
err:
	if (transport.s) {
		mnd_sprintf_free(transport.s);
		transport.s = NULL;
	}

	DBG_ERR_FMT("[%u] %.128s (trying to connect via %s)", conn->error_info->error_no, conn->error_info->error, conn->scheme.s);
	if (!conn->error_info->error_no) {
		SET_CLIENT_ERROR(conn->error_info, CR_CONNECTION_ERROR, UNKNOWN_SQLSTATE, conn->error_info->error? conn->error_info->error:"Unknown error");
		php_error_docref(NULL, E_WARNING, "[%u] %.128s (trying to connect via %s)", conn->error_info->error_no, conn->error_info->error, conn->scheme.s);
	}

	conn->m->free_contents(conn);
	MYSQLND_INC_CONN_STATISTIC(conn->stats, STAT_CONNECT_FAILURE);
	if (TRUE == local_tx_started) {
		conn->m->local_tx_end(conn, this_func, FAIL);
	}

	DBG_RETURN(FAIL);
}
/* }}} */

/* {{{ mysqlnd_azure::connect */
static enum_func_status
MYSQLND_METHOD(mysqlnd_azure, connect)(MYSQLND * conn_handle,
						const MYSQLND_CSTRING hostname,
						const MYSQLND_CSTRING username,
						const MYSQLND_CSTRING password,
						const MYSQLND_CSTRING database,
						unsigned int port,
						const MYSQLND_CSTRING socket_or_pipe,
						unsigned int mysql_flags)
{
	const size_t this_func = STRUCT_OFFSET(MYSQLND_CLASS_METHODS_TYPE(mysqlnd_conn_data), connect);
	enum_func_status ret = FAIL;
	MYSQLND_CONN_DATA ** pconn = &conn_handle->data;

	DBG_ENTER("mysqlnd_azure::connect");

	if (PASS == (*pconn)->m->local_tx_start(*pconn, this_func)) {
		mysqlnd_options4(conn_handle, MYSQL_OPT_CONNECT_ATTR_ADD, "_client_name", "mysqlnd");
		if (hostname.l > 0) {
			mysqlnd_options4(conn_handle, MYSQL_OPT_CONNECT_ATTR_ADD, "_server_host", hostname.s);
		}

		if(!MYSQLND_AZURE_G(enabled)) {
			DBG_ENTER("mysqlnd_azure::connect redirect disabled");
			ret = org_conn_d_m.connect(*pconn, hostname, username, password, database, port, socket_or_pipe, mysql_flags);
		}
		else {
			DBG_ENTER("mysqlnd_azure::connect redirect enabled");

			//first check whether the redirect info already cached
			MYSQLND_AZURE_REDIRECT_INFO* redirect_info = mysqlnd_azure_find_redirect_cache(*pconn, username.s, hostname.s, port);
			if (redirect_info != NULL) {
				DBG_ENTER("mysqlnd_azure::connect try the cached info first");
				MYSQLND_CSTRING redirect_host = { redirect_info->redirect_host, strlen(redirect_info->redirect_host) };
				MYSQLND_CSTRING redirect_user = { redirect_info->redirect_user, strlen(redirect_info->redirect_user) };

				mysqlnd_azure_set_is_using_redirect(*pconn, 1);
				ret = (*pconn)->m->connect(pconn, redirect_host, redirect_user, password, database, redirect_info->redirect_port, socket_or_pipe, mysql_flags);
				if (ret == FAIL) {
					//remove invalid cache
					mysqlnd_azure_remove_redirect_cache(*pconn, username.s, hostname.s, port);

					mysqlnd_azure_set_is_using_redirect(*pconn, 0);
					ret = (*pconn)->m->connect(pconn, hostname, username, password, database, port, socket_or_pipe, mysql_flags);
				}
			}
			else {
				ret = (*pconn)->m->connect(pconn, hostname, username, password, database, port, socket_or_pipe, mysql_flags);
			}
		}
		(*pconn)->m->local_tx_end(*pconn, this_func, FAIL);
	}
	DBG_RETURN(ret);
}
/* }}} */


/* {{{ mysqlnd_azure_minit_register_hooks */
void mysqlnd_azure_minit_register_hooks()
{
	mysqlnd_azure_plugin_id = mysqlnd_plugin_register();
	
	conn_m = mysqlnd_conn_get_methods();
	memcpy(&org_conn_m, conn_m,
	sizeof(struct st_mysqlnd_conn_methods));
	
	conn_d_m = mysqlnd_conn_data_get_methods();
	memcpy(&org_conn_d_m, conn_d_m,
	sizeof(struct st_mysqlnd_conn_data_methods));

	conn_m->connect = MYSQLND_METHOD(mysqlnd_azure, connect);
	conn_d_m->connect = MYSQLND_METHOD(mysqlnd_azure_data, connect);
	conn_d_m->dtor = MYSQLND_METHOD_PRIVATE(mysqlnd_azure_data, dtor);
}

/* }}} */
