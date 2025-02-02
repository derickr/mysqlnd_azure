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

/* {{{ mysqlnd_azure_redirect_info_dtor */
static void mysqlnd_azure_redirect_info_dtor(zval *zv)
{
	MYSQLND_AZURE_REDIRECT_INFO *redirect_info = (MYSQLND_AZURE_REDIRECT_INFO*)Z_PTR_P(zv);
	if (redirect_info->redirect_user) {
		mnd_pefree(redirect_info->redirect_user, 1);
		redirect_info->redirect_user = NULL;
	}
	if (redirect_info->redirect_host) {
		mnd_pefree(redirect_info->redirect_host, 1);
		redirect_info->redirect_host = NULL;
	}
	if (redirect_info) {
		mnd_pefree(redirect_info, 1);
		redirect_info = NULL;
	}
}
/* }}} */

/* {{{ mysqlnd_azure_get_is_using_redirect */
MYSQLND_AZURE_CONN_DATA** mysqlnd_azure_get_is_using_redirect(const MYSQLND_CONN_DATA *conn)
{
	MYSQLND_AZURE_CONN_DATA** props;
	props = (MYSQLND_AZURE_CONN_DATA**)mysqlnd_plugin_get_plugin_connection_data_data(conn, mysqlnd_azure_plugin_id);
	if (!props || !(*props)) {
		*props = mnd_pecalloc(1, sizeof(MYSQLND_AZURE_CONN_DATA), conn->persistent);
		(*props)->is_using_redirect = 0;
	}
	return props;
}
/* }}} */

/* {{{ mysqlnd_azure_set_is_using_redirect */
MYSQLND_AZURE_CONN_DATA** mysqlnd_azure_set_is_using_redirect(MYSQLND_CONN_DATA *conn, zend_bool is_using_redirect)
{
	MYSQLND_AZURE_CONN_DATA** props;
	props = (MYSQLND_AZURE_CONN_DATA**)mysqlnd_plugin_get_plugin_connection_data_data(conn, mysqlnd_azure_plugin_id);
	if (!props || !(*props)) {
		*props = mnd_pecalloc(1, sizeof(MYSQLND_AZURE_CONN_DATA), conn->persistent);
		(*props)->is_using_redirect = 0;
	}
	(*props)->is_using_redirect = is_using_redirect;
	return props;
}
/* }}} */

/* {{{ mysqlnd_azure_add_redirect_cache */
enum_func_status mysqlnd_azure_add_redirect_cache(const MYSQLND_CONN_DATA* conn, const char* user, const char* host, int port, const char* redirect_user, const char* redirect_host, int redirect_port)
{
	if (MYSQLND_AZURE_G(redirectCache) == NULL) {
		MYSQLND_AZURE_G(redirectCache) = mnd_pemalloc(sizeof(HashTable), 1);
		zend_hash_init(MYSQLND_AZURE_G(redirectCache), 0, NULL, mysqlnd_azure_redirect_info_dtor, 1);
	}

	char *key = NULL;
	mnd_sprintf(&key, MAX_REDIRECT_HOST_LEN+ MAX_REDIRECT_USER_LEN+8, "%s_%s_%d", user, host, port);

	MYSQLND_AZURE_REDIRECT_INFO* redirect_info = pemalloc(sizeof(MYSQLND_AZURE_REDIRECT_INFO), conn->persistent);
	redirect_info->redirect_user = mnd_pestrndup(redirect_user, strlen(redirect_user), conn->persistent);
	redirect_info->redirect_host = mnd_pestrndup(redirect_host, strlen(redirect_host), conn->persistent);
	redirect_info->redirect_port = redirect_port;

	zend_hash_str_update_ptr(MYSQLND_AZURE_G(redirectCache), key, strlen(key), redirect_info);

	mnd_sprintf_free(key);

    return PASS;
}
/* }}} */

/* {{{ mysqlnd_azure_remove_redirect_cache */
enum_func_status mysqlnd_azure_remove_redirect_cache(const MYSQLND_CONN_DATA* conn, const char* user, const char* host, int port)
{
	if (MYSQLND_AZURE_G(redirectCache) == NULL)
		return PASS;

	char *key = NULL;
	mnd_sprintf(&key, MAX_REDIRECT_HOST_LEN + MAX_REDIRECT_USER_LEN + 8, "%s_%s_%d", user, host, port);

	zend_hash_str_del(MYSQLND_AZURE_G(redirectCache), key, strlen(key));

	mnd_sprintf_free(key);

	return PASS;
}
/* }}} */

/* {{{ mysqlnd_azure_find_redirect_cache */
MYSQLND_AZURE_REDIRECT_INFO* mysqlnd_azure_find_redirect_cache(const MYSQLND_CONN_DATA* conn, const char* user, const char* host, int port)
{
	if (MYSQLND_AZURE_G(redirectCache) == NULL)
		return NULL;

	char *key = NULL;
	mnd_sprintf(&key, MAX_REDIRECT_HOST_LEN + MAX_REDIRECT_USER_LEN + 8, "%s_%s_%d", user, host, port);

	void* zv_dest = zend_hash_str_find_ptr(MYSQLND_AZURE_G(redirectCache), key, strlen(key));
	mnd_sprintf_free(key);

	return (MYSQLND_AZURE_REDIRECT_INFO*)zv_dest;
}
/* }}} */
