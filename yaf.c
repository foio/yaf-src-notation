/*

  | Yet Another Framework                                                |
  +----------------------------------------------------------------------+
  | This source file is subject to version 3.01 of the PHP license,      |
  | that is bundled with this package in the file LICENSE, and is        |
  | available through the world-wide-web at the following url:           |
  | http://www.php.net/license/3_01.txt                                  |
  | If you did not receive a copy of the PHP license and are unable to   |
  | obtain it through the world-wide-web, please send a note to          |
  | license@php.net so we can mail you a copy immediately.               |
  +----------------------------------------------------------------------+
  | Author: Xinchen Hui  <laruence@php.net>                              |
  +----------------------------------------------------------------------+
*/

/* $Id: yaf.c 329002 2013-01-07 12:55:53Z laruence $ */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php.h"
#include "php_ini.h"
#include "main/SAPI.h"
#include "Zend/zend_alloc.h"
#include "ext/standard/info.h"
#include "ext/standard/php_string.h"

#include "php_yaf.h"
#include "yaf_logo.h"
#include "yaf_loader.h"
#include "yaf_exception.h"
#include "yaf_application.h"
#include "yaf_dispatcher.h"
#include "yaf_config.h"
#include "yaf_view.h"
#include "yaf_controller.h"
#include "yaf_action.h"
#include "yaf_request.h"
#include "yaf_response.h"
#include "yaf_router.h"
#include "yaf_bootstrap.h"
#include "yaf_plugin.h"
#include "yaf_registry.h"
#include "yaf_session.h"

/* 声明模块名称，用户避免命名冲突 */
ZEND_DECLARE_MODULE_GLOBALS(yaf);

/* {{{ yaf_functions[]
*/
zend_function_entry yaf_functions[] = {
	{NULL, NULL, NULL}
};
/* }}} */

/** {{{ PHP_INI_MH(OnUpdateSeparator)
 */
/*	当php.ini的yaf.name_separator配置被改动时调用的回调函数  */
//  通过YAF_G线程安全地访问变量,具体信息请参考:http://www.laruence.com/2008/08/03/201.html
PHP_INI_MH(OnUpdateSeparator) {
	YAF_G(name_separator) = new_value; 
	YAF_G(name_separator_len) = new_value_length;
	return SUCCESS;
}
/* }}} */

/** {{{ PHP_INI
 */
/* 设置扩展配置参数的默认值并设置默认值改变时的回调函数 */
PHP_INI_BEGIN()
	STD_PHP_INI_ENTRY("yaf.library",         	"",  PHP_INI_ALL, OnUpdateString, global_library, zend_yaf_globals, yaf_globals)
	STD_PHP_INI_BOOLEAN("yaf.action_prefer",   	"0", PHP_INI_ALL, OnUpdateBool, action_prefer, zend_yaf_globals, yaf_globals)
	STD_PHP_INI_BOOLEAN("yaf.lowcase_path",    	"0", PHP_INI_ALL, OnUpdateBool, lowcase_path, zend_yaf_globals, yaf_globals)
	STD_PHP_INI_BOOLEAN("yaf.use_spl_autoload", "0", PHP_INI_ALL, OnUpdateBool, use_spl_autoload, zend_yaf_globals, yaf_globals)
	STD_PHP_INI_ENTRY("yaf.forward_limit", 		"5", PHP_INI_ALL, OnUpdateLongGEZero, forward_limit, zend_yaf_globals, yaf_globals)
	STD_PHP_INI_BOOLEAN("yaf.name_suffix", 		"1", PHP_INI_ALL, OnUpdateBool, name_suffix, zend_yaf_globals, yaf_globals)
	PHP_INI_ENTRY("yaf.name_separator", 		"",  PHP_INI_ALL, OnUpdateSeparator)
	STD_PHP_INI_BOOLEAN("yaf.cache_config",    	"0", PHP_INI_SYSTEM, OnUpdateBool, cache_config, zend_yaf_globals, yaf_globals)
/* {{{ This only effects internally */
	STD_PHP_INI_BOOLEAN("yaf.st_compatible",     "0", PHP_INI_ALL, OnUpdateBool, st_compatible, zend_yaf_globals, yaf_globals)
/* }}} */
	STD_PHP_INI_ENTRY("yaf.environ",        	"product", PHP_INI_SYSTEM, OnUpdateString, environ, zend_yaf_globals, yaf_globals)
#ifdef YAF_HAVE_NAMESPACE
	STD_PHP_INI_BOOLEAN("yaf.use_namespace",   	"0", PHP_INI_SYSTEM, OnUpdateBool, use_namespace, zend_yaf_globals, yaf_globals)
#endif
PHP_INI_END();
/* }}} */

/** {{{ PHP_GINIT_FUNCTION
*/
//初始化全局变量yaf,PHP_GINIT_FUNCTION在每次创建新的线程时会创建,yaf全局变量在php_yaf.h中定义
PHP_GINIT_FUNCTION(yaf) 
{
	yaf_globals->autoload_started   = 0;
	yaf_globals->configs			= NULL;
	yaf_globals->directory			= NULL;
	yaf_globals->local_library		= NULL;
	yaf_globals->ext				= YAF_DEFAULT_EXT;
	yaf_globals->view_ext			= YAF_DEFAULT_VIEW_EXT;
	yaf_globals->default_module		= YAF_ROUTER_DEFAULT_MODULE;
	yaf_globals->default_controller = YAF_ROUTER_DEFAULT_CONTROLLER;
	yaf_globals->default_action		= YAF_ROUTER_DEFAULT_ACTION;
	yaf_globals->bootstrap			= YAF_DEFAULT_BOOTSTRAP;
	yaf_globals->modules			= NULL;
	yaf_globals->default_route      = NULL;
	yaf_globals->suppressing_warning = 0;
}
/* }}} */

/** {{{ PHP_MINIT_FUNCTION
*/
//定义模块初始化函数
PHP_MINIT_FUNCTION(yaf)
{
	//注册ini设置变量
	REGISTER_INI_ENTRIES();

	//注册logo，5.5版本以上已经移除
#if PHP_MAJOR_VERSION == 5 && PHP_MINOR_VERSION < 5 
	php_register_info_logo(YAF_LOGO_GUID, YAF_LOGO_MIME_TYPE, yaf_logo, sizeof(yaf_logo));
#endif
	//注册扩展所用的常量,主要是版本信息以及错误码
#ifdef YAF_HAVE_NAMESPACE
	if(YAF_G(use_namespace)) {

		REGISTER_STRINGL_CONSTANT("YAF\\VERSION", PHP_YAF_VERSION, 	sizeof(PHP_YAF_VERSION) - 1, 	CONST_PERSISTENT | CONST_CS);
		REGISTER_STRINGL_CONSTANT("YAF\\ENVIRON", YAF_G(environ), strlen(YAF_G(environ)), 	CONST_PERSISTENT | CONST_CS);

		REGISTER_LONG_CONSTANT("YAF\\ERR\\STARTUP_FAILED", 		YAF_ERR_STARTUP_FAILED, CONST_PERSISTENT | CONST_CS);
		REGISTER_LONG_CONSTANT("YAF\\ERR\\ROUTE_FAILED", 		YAF_ERR_ROUTE_FAILED, CONST_PERSISTENT | CONST_CS);
		REGISTER_LONG_CONSTANT("YAF\\ERR\\DISPATCH_FAILED", 	YAF_ERR_DISPATCH_FAILED, CONST_PERSISTENT | CONST_CS);
		REGISTER_LONG_CONSTANT("YAF\\ERR\\AUTOLOAD_FAILED", 	YAF_ERR_AUTOLOAD_FAILED, CONST_PERSISTENT | CONST_CS);
		REGISTER_LONG_CONSTANT("YAF\\ERR\\NOTFOUND\\MODULE", 	YAF_ERR_NOTFOUND_MODULE, CONST_PERSISTENT | CONST_CS);
		REGISTER_LONG_CONSTANT("YAF\\ERR\\NOTFOUND\\CONTROLLER",YAF_ERR_NOTFOUND_CONTROLLER, CONST_PERSISTENT | CONST_CS);
		REGISTER_LONG_CONSTANT("YAF\\ERR\\NOTFOUND\\ACTION", 	YAF_ERR_NOTFOUND_ACTION, CONST_PERSISTENT | CONST_CS);
		REGISTER_LONG_CONSTANT("YAF\\ERR\\NOTFOUND\\VIEW", 		YAF_ERR_NOTFOUND_VIEW, CONST_PERSISTENT | CONST_CS);
		REGISTER_LONG_CONSTANT("YAF\\ERR\\CALL_FAILED",			YAF_ERR_CALL_FAILED, CONST_PERSISTENT | CONST_CS);
		REGISTER_LONG_CONSTANT("YAF\\ERR\\TYPE_ERROR",			YAF_ERR_TYPE_ERROR, CONST_PERSISTENT | CONST_CS);

	} else {
#endif
		REGISTER_STRINGL_CONSTANT("PHP_YAF_VERSION", PHP_YAF_VERSION, 	sizeof(PHP_YAF_VERSION) - 1, 	CONST_PERSISTENT | CONST_CS);
		REGISTER_STRINGL_CONSTANT("YAF_ENVIRON", YAF_G(environ),strlen(YAF_G(environ)), 	CONST_PERSISTENT | CONST_CS);

		REGISTER_LONG_CONSTANT("YAF_ERR_STARTUP_FAILED", 		YAF_ERR_STARTUP_FAILED, CONST_PERSISTENT | CONST_CS);
		REGISTER_LONG_CONSTANT("YAF_ERR_ROUTE_FAILED", 			YAF_ERR_ROUTE_FAILED, CONST_PERSISTENT | CONST_CS);
		REGISTER_LONG_CONSTANT("YAF_ERR_DISPATCH_FAILED", 		YAF_ERR_DISPATCH_FAILED, CONST_PERSISTENT | CONST_CS);
		REGISTER_LONG_CONSTANT("YAF_ERR_AUTOLOAD_FAILED", 		YAF_ERR_AUTOLOAD_FAILED, CONST_PERSISTENT | CONST_CS);
		REGISTER_LONG_CONSTANT("YAF_ERR_NOTFOUND_MODULE", 		YAF_ERR_NOTFOUND_MODULE, CONST_PERSISTENT | CONST_CS);
		REGISTER_LONG_CONSTANT("YAF_ERR_NOTFOUND_CONTROLLER", 	YAF_ERR_NOTFOUND_CONTROLLER, CONST_PERSISTENT | CONST_CS);
		REGISTER_LONG_CONSTANT("YAF_ERR_NOTFOUND_ACTION", 		YAF_ERR_NOTFOUND_ACTION, CONST_PERSISTENT | CONST_CS);
		REGISTER_LONG_CONSTANT("YAF_ERR_NOTFOUND_VIEW", 		YAF_ERR_NOTFOUND_VIEW, CONST_PERSISTENT | CONST_CS);
		REGISTER_LONG_CONSTANT("YAF_ERR_CALL_FAILED",			YAF_ERR_CALL_FAILED, CONST_PERSISTENT | CONST_CS);
		REGISTER_LONG_CONSTANT("YAF_ERR_TYPE_ERROR",			YAF_ERR_TYPE_ERROR, CONST_PERSISTENT | CONST_CS);
#ifdef YAF_HAVE_NAMESPACE
	}
#endif
	//依次启动yaf框架的模块
	/* startup components */
	YAF_STARTUP(application);  //最终展开为调用函数:zm_startup_yaf_appliction(type, module_number TSRMLS_CC),实际调用的是yaf_application.c中定义的YAF_STARTUP_FUNCTION(application),主要作用为定义类yaf_application的属性和方法
	YAF_STARTUP(bootstrap);//与YAF_STARTUP(application)类似
	YAF_STARTUP(dispatcher);//与YAF_STARTUP(application)类似
	YAF_STARTUP(loader);//与YAF_STARTUP(application)类似
	YAF_STARTUP(request);//与YAF_STARTUP(application)类似
	YAF_STARTUP(response);//与YAF_STARTUP(application)类似
	YAF_STARTUP(controller);//与YAF_STARTUP(application)类似
	YAF_STARTUP(action);//与YAF_STARTUP(application)类似
	YAF_STARTUP(config);//与YAF_STARTUP(application)类似
	YAF_STARTUP(view);//与YAF_STARTUP(application)类似
	YAF_STARTUP(router);//与YAF_STARTUP(application)类似
	YAF_STARTUP(plugin);//与YAF_STARTUP(application)类似
	YAF_STARTUP(registry);//与YAF_STARTUP(application)类似
	YAF_STARTUP(session);//与YAF_STARTUP(application)类似
	YAF_STARTUP(exception);//与YAF_STARTUP(application)类似

	return SUCCESS;
}
/* }}} */

/** {{{ PHP_MSHUTDOWN_FUNCTION
*/
//定义模块销毁函数
PHP_MSHUTDOWN_FUNCTION(yaf)
{
	UNREGISTER_INI_ENTRIES();

	if (YAF_G(configs)) {//configs为yaf全局变量的唯一Hashtab，需要处理
		zend_hash_destroy(YAF_G(configs));
		pefree(YAF_G(configs), 1);
	}

	return SUCCESS;
}
/* }}} */

/** {{{ PHP_RINIT_FUNCTION
*/
//定义请求初始化函数
PHP_RINIT_FUNCTION(yaf)
{
	//初始化请求相关的全局变量
	YAF_G(running)			= 0;
	YAF_G(in_exception)		= 0;
	YAF_G(throw_exception)	= 1;
	YAF_G(catch_exception)	= 0;
	YAF_G(directory)		= NULL;
	YAF_G(bootstrap)		= NULL;
	YAF_G(local_library)	= NULL;
	YAF_G(local_namespaces)	= NULL;
	YAF_G(modules)			= NULL;
	YAF_G(base_uri)			= NULL;
	YAF_G(view_directory)	= NULL;
#if ((PHP_MAJOR_VERSION == 5) && (PHP_MINOR_VERSION < 4))
	YAF_G(buffer)			= NULL;
	YAF_G(owrite_handler)	= NULL;
	YAF_G(buf_nesting)		= 0;
#endif

	return SUCCESS;
}
/* }}} */

/** {{{ PHP_RSHUTDOWN_FUNCTION
*/
//定义请求结束后的销毁函数
PHP_RSHUTDOWN_FUNCTION(yaf)
{
    //char*类型变量，指向运行时动态分配的内存，一次请求处理后需要手动释放，否则会造成内存泄露
	if (YAF_G(directory)) {
		efree(YAF_G(directory));
	}
	if (YAF_G(local_library)) {
		efree(YAF_G(local_library));
	}
	if (YAF_G(local_namespaces)) {
		efree(YAF_G(local_namespaces));
	}
	if (YAF_G(bootstrap)) {
		efree(YAF_G(bootstrap));
	}
	if (YAF_G(modules)) {
		zval_ptr_dtor(&(YAF_G(modules)));
	}
	if (YAF_G(base_uri)) {
		efree(YAF_G(base_uri));
	}
	if (YAF_G(view_directory)) {
		efree(YAF_G(view_directory));
	}
	YAF_G(default_route) = NULL;

	return SUCCESS;
}
/* }}} */

/** {{{ PHP_MINFO_FUNCTION
*/
//php -i 或者phpinfo() 答应出来的模块相关信息
PHP_MINFO_FUNCTION(yaf)
{
	php_info_print_table_start();
	if (PG(expose_php) && !sapi_module.phpinfo_as_text) {//如果作为文字不显示logo,否则显示logo
		php_info_print_table_header(2, "yaf support", YAF_LOGO_IMG"enabled");
	} else {
		php_info_print_table_header(2, "yaf support", "enabled");
	}


	php_info_print_table_row(2, "Version", PHP_YAF_VERSION);//版本信息
	php_info_print_table_row(2, "Supports", YAF_SUPPORT_URL);//帮助文档
	php_info_print_table_end();

	DISPLAY_INI_ENTRIES();
}
/* }}} */

/** {{{ DL support
 */
#ifdef COMPILE_DL_YAF
ZEND_GET_MODULE(yaf)
#endif
/* }}} */

/** {{{ module depends
 */
#if ZEND_MODULE_API_NO >= 20050922
//定义yaf模块对其他模块的依赖关系与加载顺序 参见http://www.laruence.com/2009/08/18/1042.html
zend_module_dep yaf_deps[] = {
	ZEND_MOD_REQUIRED("spl")//首先加载spl,必须
	ZEND_MOD_REQUIRED("pcre")//其次加载pcre,必须
	ZEND_MOD_OPTIONAL("session")//最后加载session,可选
	{NULL, NULL, NULL}
};
#endif
/* }}} */

/** {{{ yaf_module_entry
*/
zend_module_entry yaf_module_entry = {
#if ZEND_MODULE_API_NO >= 20050922
	STANDARD_MODULE_HEADER_EX, NULL,
	yaf_deps,//应用yaf_deps中定义的依赖关系
#else
	STANDARD_MODULE_HEADER,
#endif
	"yaf",//扩展名称
	yaf_functions,//扩展定义的可在php调用的函数，yaf扩展没有定义函数，参见yaf_functions定义
	PHP_MINIT(yaf),//注册模块加载初始化函数
	PHP_MSHUTDOWN(yaf),//注册模块卸载的销毁函数
	PHP_RINIT(yaf),//注册请求初始化函数
	PHP_RSHUTDOWN(yaf),//注册请求结束后的销毁函数
	PHP_MINFO(yaf),//注册phpinfo()答应的模块信息函数
	PHP_YAF_VERSION,//yaf扩展版本号
	PHP_MODULE_GLOBALS(yaf),//注册模块全局变量
	PHP_GINIT(yaf),//注册模块全局变量初始化函数
	NULL,
	NULL,
	STANDARD_MODULE_PROPERTIES_EX
};
/* }}} */

/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: noet sw=4 ts=4 fdm=marker
 * vim<600: noet sw=4 ts=4
 */
