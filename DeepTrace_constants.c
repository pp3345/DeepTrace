/*
	+-----------------------------------------------------------------------+
	| DeepTrace ( Homepage: https://www.snapserv.net/ )					 	|
	+-----------------------------------------------------------------------+
	| Copyright (c) 2012 P. Mathis (pmathis@snapserv.net)                   |
	+-----------------------------------------------------------------------+
	| License info (CC BY-NC-SA 3.0)										|
	|																		|
	| This code is licensed via a Creative Commons Licence:					|
	| http://creativecommons.org/licenses/by-nc-sa/3.0/						|
	| Means:	- You may alter the code, but have to give the changes back |
	|			- You may not use this work for commercial purposes			|
	|			- You must attribute the work in the manner specified by	|
	|				the author or licensor.									|
	+-----------------------------------------------------------------------+
	| If you like to use this code commercially,							|
	| please contact pmathis@snapserv.net									|
	+-----------------------------------------------------------------------+
*/

#include "php_DeepTrace.h"

// dt_remove_constant
// Parameters: string constantName
// Return value: bool success
PHP_FUNCTION(dt_remove_constant)
{
	zend_constant *constant;
	char *constName, *lcase;
	int len;
	int caseSensitive;

	// Get parameters
	if(zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s", &constName, &len) == FAILURE) {
		RETURN_FALSE;
	}

	// Get constant
	if(zend_hash_find(EG(zend_constants), constName, len + 1, (void**) &constant) == FAILURE) {
		lcase = zend_str_tolower_dup(constName, len);
		if(zend_hash_find(EG(zend_constants), lcase, len + 1, (void**) &constant) == FAILURE) {
			php_error_docref(NULL TSRMLS_CC, E_WARNING, "Constant %s not found.", constName);
			efree(lcase);
			RETURN_FALSE;
		}

		if((constant->flags & CONST_CS)) {
			php_error_docref(NULL TSRMLS_CC, E_WARNING, "Constant %s is case-sensitive.", constant->name);
			efree(lcase);
			RETURN_FALSE;
		}
		efree(lcase);
	}

	// Safety warning for persistent constants
	if(constant->flags & CONST_PERSISTENT) {
		php_error_docref(NULL TSRMLS_CC, E_NOTICE, "Constant %s is persistent. You should not modify it.", constant->name);
	}

	// Is case-Sensitive?
	caseSensitive = ((constant->flags & CONST_CS) == 0);
	if(caseSensitive) {
		constName = zend_str_tolower_dup(constant->name, constant->name_len);
	} else {
		constName = constant->name;
	}

	// Remove constant from cache
	if(DEEPTRACE_G(constantCache)) {
		zend_hash_del(DEEPTRACE_G(constantCache), constName, constant->name_len);
	}

	// Delete constant
	if(zend_hash_del(EG(zend_constants), constName, constant->name_len) == FAILURE) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Could not remove constant %s.", constName);
		if(caseSensitive) efree(constName);
		RETURN_FALSE;
	}

	// Free memory
	if(caseSensitive) efree(constName);
	RETURN_TRUE;
}

// dt_get_cache_size
// Get Zend RTC size
PHP_FUNCTION(dt_get_cache_size) {
	RETVAL_LONG(EG(active_op_array)->last_cache_slot);
	return;
}

// zend_get_special_constant
// Get special constant (const)
static int zend_get_special_constant(const char *name, uint name_len, zend_constant **c TSRMLS_DC)
{
	int ret;
	static char haltoff[] = "__COMPILER_HALT_OFFSET__";

	if (!EG(in_execution)) {
		return 0;
	} else if (name_len == sizeof("__CLASS__")-1 &&
	          !memcmp(name, "__CLASS__", sizeof("__CLASS__")-1)) {
		zend_constant tmp;

		/* Returned constants may be cached, so they have to be stored */
		if (EG(scope) && EG(scope)->name) {
			int const_name_len;
			char *const_name;
			ALLOCA_FLAG(use_heap)
			
			const_name_len = sizeof("\0__CLASS__") + EG(scope)->name_length;
			const_name = do_alloca(const_name_len, use_heap);
			memcpy(const_name, "\0__CLASS__", sizeof("\0__CLASS__")-1);
			zend_str_tolower_copy(const_name + sizeof("\0__CLASS__")-1, EG(scope)->name, EG(scope)->name_length);
			if (zend_hash_find(EG(zend_constants), const_name, const_name_len, (void**)c) == FAILURE) {
				zend_hash_add(EG(zend_constants), const_name, const_name_len, (void*)&tmp, sizeof(zend_constant), (void**)c);
				memset(*c, 0, sizeof(zend_constant));
				Z_STRVAL((**c).value) = estrndup(EG(scope)->name, EG(scope)->name_length);
				Z_STRLEN((**c).value) = EG(scope)->name_length;
				Z_TYPE((**c).value) = IS_STRING;
			}
			free_alloca(const_name, use_heap);
		} else {
			if (zend_hash_find(EG(zend_constants), "\0__CLASS__", sizeof("\0__CLASS__"), (void**)c) == FAILURE) {
				zend_hash_add(EG(zend_constants), "\0__CLASS__", sizeof("\0__CLASS__"), (void*)&tmp, sizeof(zend_constant), (void**)c);
				memset(*c, 0, sizeof(zend_constant));
				Z_STRVAL((**c).value) = estrndup("", 0);
				Z_STRLEN((**c).value) = 0;
				Z_TYPE((**c).value) = IS_STRING;
			}
		}
		return 1;
	} else if (name_len == sizeof("__COMPILER_HALT_OFFSET__")-1 &&
	          !memcmp(name, "__COMPILER_HALT_OFFSET__", sizeof("__COMPILER_HALT_OFFSET__")-1)) {
		const char *cfilename;
		char *haltname;
		int len, clen;

		cfilename = zend_get_executed_filename(TSRMLS_C);
		clen = strlen(cfilename);
		/* check for __COMPILER_HALT_OFFSET__ */
		zend_mangle_property_name(&haltname, &len, haltoff,
			sizeof("__COMPILER_HALT_OFFSET__") - 1, cfilename, clen, 0);
		ret = zend_hash_find(EG(zend_constants), haltname, len+1, (void **) c);
		efree(haltname);
		return (ret == SUCCESS);
	} else {
		return 0;
	}
}

// zend_quick_get_constant
// Get constant from literal
zend_constant *zend_quick_get_constant(const zend_literal *key, ulong flags TSRMLS_DC)
{
	zend_constant *c;

	if (zend_hash_quick_find(EG(zend_constants), Z_STRVAL(key->constant), Z_STRLEN(key->constant) + 1, key->hash_value, (void **) &c) == FAILURE) {
		key++;
		if (zend_hash_quick_find(EG(zend_constants), Z_STRVAL(key->constant), Z_STRLEN(key->constant) + 1, key->hash_value, (void **) &c) == FAILURE ||
		    (c->flags & CONST_CS) != 0) {
			if ((flags & (IS_CONSTANT_IN_NAMESPACE|IS_CONSTANT_UNQUALIFIED)) == (IS_CONSTANT_IN_NAMESPACE|IS_CONSTANT_UNQUALIFIED)) {
				key++;
				if (zend_hash_quick_find(EG(zend_constants), Z_STRVAL(key->constant), Z_STRLEN(key->constant) + 1, key->hash_value, (void **) &c) == FAILURE) {
				    key++;
					if (zend_hash_quick_find(EG(zend_constants), Z_STRVAL(key->constant), Z_STRLEN(key->constant) + 1, key->hash_value, (void **) &c) == FAILURE ||
					    (c->flags & CONST_CS) != 0) {

						key--;
						if (!zend_get_special_constant(Z_STRVAL(key->constant), Z_STRLEN(key->constant), &c TSRMLS_CC)) {
							return NULL;
						}
					}
				}
			} else {
				key--;
				if (!zend_get_special_constant(Z_STRVAL(key->constant), Z_STRLEN(key->constant), &c TSRMLS_CC)) {
					return NULL;
				}
			}
		}
	}
	return c;
}

// DeepTrace_constant_handler
// Overrides intern RunTimeCache
int DeepTrace_constant_handler(ZEND_OPCODE_HANDLER_ARGS) {
	char* constName;
	char* className;
	char* combinedName;
	int constLen;
	int classLen;
	int combinedLen;
	void* cachePtr;
	zend_constant *c;

	// Get name and length
	constName = Z_STRVAL(EX(opline)->op2.literal->constant);
	constLen = Z_STRLEN(EX(opline)->op2.literal->constant) + 1;

	// Check if cache exists
	if(!DEEPTRACE_G(constantCache)) {
		ALLOC_HASHTABLE(DEEPTRACE_G(constantCache));
		zend_hash_init(DEEPTRACE_G(constantCache), 4, NULL, NULL, 0);
	}

	// Get cache pointer
	if(EX(opline)->op1_type == IS_UNUSED) {
		if(zend_hash_find(DEEPTRACE_G(constantCache), constName, constLen, (void**) &cachePtr)) {
			// Add normal constant to cache
			// Detect pointer
			c = zend_quick_get_constant(EX(opline)->op2.literal + 1, EX(opline)->extended_value TSRMLS_CC);
			cachePtr = (void*) c;

			// Add to cache
			#if DEEPTRACE_DEBUG_CACHE == 1
				zend_printf("[DT Cache - Add] %s @ %d\n", constName, cachePtr);
			#endif
			zend_hash_add(DEEPTRACE_G(constantCache), constName, constLen, (void**) &cachePtr, sizeof(void*), NULL);
		} else {
			// Get pointer from cache
			#if DEEPTRACE_DEBUG_CACHE == 1
				zend_printf("[DT Cache - Get] %s @ %d\n", constName, cachePtr);
			#endif
			cachePtr = *((void**) cachePtr);
		}
	} else {
		// Build constant name
		className = Z_STRVAL_P(EX(opline)->op1.zv);
		classLen = Z_STRLEN_P(EX(opline)->op1.zv) + 1;
		combinedName = emalloc(classLen + constLen + 1);
		combinedLen = classLen + constLen + 1;
		sprintf(combinedName, "%s%c%s%c", className, '\0', constName, '\0');

		if(zend_hash_find(DEEPTRACE_G(constantCache), combinedName, combinedLen, (void**) &cachePtr)) {
			// Add class constant to cache
			// Get class
			zend_class_entry *ce;
			zval **value;
			if (CACHED_PTR(EX(opline)->op2.literal->cache_slot)) {
				value = CACHED_PTR(EX(opline)->op2.literal->cache_slot);
				ZVAL_COPY_VALUE(&EX_T(EX(opline)->result.var).tmp_var, *value);
				zval_copy_ctor(&EX_T(EX(opline)->result.var).tmp_var);
				CHECK_EXCEPTION();
				ZEND_VM_NEXT_OPCODE();
			} else if (CACHED_PTR(EX(opline)->op1.literal->cache_slot)) {
				ce = CACHED_PTR(EX(opline)->op1.literal->cache_slot);
			} else {
				ce = zend_fetch_class_by_name(className, classLen, EX(opline)->op1.literal + 1, EX(opline)->extended_value TSRMLS_CC);
				if (UNEXPECTED(ce == NULL)) {
					CHECK_EXCEPTION();
					ZEND_VM_NEXT_OPCODE();
				}
				CACHE_PTR(EX(opline)->op1.literal->cache_slot, ce);
			}

			// Detect pointer
			zend_hash_quick_find(&ce->constants_table, constName, constLen, Z_HASH_P(EX(opline)->op2.zv), (void **) &value);
			cachePtr = (void*) value;

			// Add to cache
			#if DEEPTRACE_DEBUG_CACHE == 1
				zend_printf("[DT Cache - Class Add] %s in %s @ %d\n", constName, className, cachePtr);
			#endif
			zend_hash_add(DEEPTRACE_G(constantCache), combinedName, combinedLen, (void**) &cachePtr, sizeof(void*), NULL);
		} else {
			// Get pointer from cache
			#if DEEPTRACE_DEBUG_CACHE == 1
				zend_printf("[DT Cache - Class Get] %s in %s @ %d\n", constName, className, cachePtr);
			#endif
			cachePtr = *((void**) cachePtr);	
		}

		// Free constant name
		efree(combinedName);
	}
	

	// Modify Zend cache
	EG(active_op_array)->run_time_cache[EX(opline)->op2.literal->cache_slot] = cachePtr;
	return ZEND_USER_OPCODE_DISPATCH;
}

// DeepTrace_destroy_cache_entries
int DeepTrace_destroy_cache_entries(zend_hash_key *hash_key TSRMLS_DC) {
	return ZEND_HASH_APPLY_REMOVE;
}