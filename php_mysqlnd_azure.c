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

#include "php.h"
#include "ext/standard/info.h"
#include "mysqlnd_azure.h"
#include "php_mysqlnd_azure.h"
#include "ext/mysqlnd/mysqlnd_ext_plugin.h"

ZEND_DECLARE_MODULE_GLOBALS(mysqlnd_azure)

/* {{{ PHP_GINIT_FUNCTION */
static PHP_GINIT_FUNCTION(mysqlnd_azure)
{
#if defined(COMPILE_DL_MYSQLND_AZURE) && defined(ZTS)
	ZEND_TSRMLS_CACHE_UPDATE();
#endif
	mysqlnd_azure_globals->enabled = 0;
	mysqlnd_azure_globals->redirectCache = NULL;
}
/* }}} */

/* {{{ PHP_GSHUTDOWN_FUNCTION */
static PHP_GSHUTDOWN_FUNCTION(mysqlnd_azure)
{
	if (mysqlnd_azure_globals->redirectCache) {
		zend_hash_destroy(mysqlnd_azure_globals->redirectCache);
		mnd_pefree(mysqlnd_azure_globals->redirectCache, 1);
		mysqlnd_azure_globals->redirectCache = NULL;
	}
}
/* }}} */

/* {{{ PHP_INI */
/*
	It is handy to allow users to disable any mysqlnd plugin globally - not only for debugging :-)
	Because we register our plugin in MINIT changes to mysqlnd_ed.enabled shall be bound to
	INI_SYSTEM (and PHP restarts).
*/
PHP_INI_BEGIN()
STD_PHP_INI_ENTRY("mysqlnd_azure.enabled", "0", PHP_INI_ALL, OnUpdateBool, enabled, zend_mysqlnd_azure_globals, mysqlnd_azure_globals)
PHP_INI_END()
/* }}} */

/* {{{ PHP_MINIT_FUNCTION
 */
static PHP_MINIT_FUNCTION(mysqlnd_azure)
{
  /* globals, ini entries, resources, classes */
  REGISTER_INI_ENTRIES();
  /* register mysqlnd plugin */
  mysqlnd_azure_minit_register_hooks();

  return SUCCESS;
}

/* {{{ PHP_MSHUTDOWN_FUNCTION
 */
static PHP_MSHUTDOWN_FUNCTION(mysqlnd_azure)
{
	UNREGISTER_INI_ENTRIES();

	return SUCCESS;
}

/* {{{ PHP_MINFO_FUNCTION
 */
PHP_MINFO_FUNCTION(mysqlnd_azure)
{
	php_info_print_table_start();
	php_info_print_table_header(2, "mysqlnd_azure", "enabled");
	php_info_print_table_row(2, "enabled", MYSQLND_AZURE_G(enabled)? "Yes":"No");
	php_info_print_table_end();
}
/* }}} */

static const zend_module_dep mysqlnd_azure_deps[] = {
	ZEND_MOD_REQUIRED("mysqlnd")
	ZEND_MOD_END
};

/* {{{ mysqlnd_azure_module_entry*/
zend_module_entry mysqlnd_azure_module_entry = {
	STANDARD_MODULE_HEADER_EX,
	NULL,
	mysqlnd_azure_deps,
	EXT_MYSQLND_AZURE_NAME,
	NULL,
	PHP_MINIT(mysqlnd_azure),
	PHP_MSHUTDOWN(mysqlnd_azure),
	NULL,
	NULL,
	PHP_MINFO(mysqlnd_azure),
	EXT_MYSQLND_AZURE_VERSION,
	PHP_MODULE_GLOBALS(mysqlnd_azure),
	PHP_GINIT(mysqlnd_azure),
	PHP_GSHUTDOWN(mysqlnd_azure),
	NULL,
	STANDARD_MODULE_PROPERTIES_EX
};
/* }}} */

#ifdef COMPILE_DL_MYSQLND_AZURE
#ifdef ZTS
ZEND_TSRMLS_CACHE_DEFINE()
#endif
ZEND_GET_MODULE(mysqlnd_azure)
#endif
