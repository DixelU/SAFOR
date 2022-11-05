#ifndef SAFOR_VERSION_H
#define SAFOR_VERSION_H

#define VER_MAJOR	1
#define VER_MINOR	3
#define VER_RELEASE	4
#define VER_BUILD	0
#define EXTR(x) x
#define VAL(x) EXTR(x)
#define STR_DIRECT(x) #x
#define STR(x) STR_DIRECT(x)
#define VER_STRING	STR(VAL(VER_MAJOR)) "." STR(VAL(VER_MINOR)) "." STR(VAL(VER_RELEASE)) "." STR(VAL(VER_BUILD))
#define COMPANY_NAME	"DixelU"
#define FILE_VERSION	VER_STRING
#define FILE_DESCRIPTION	"SAF Overlaps Remover"
#define INTERNAL_NAME	"SAFOR"
#define LEGAL_COPYRIGHT	""
#define LEGAL_TRADEMARKS	""
#define ORIGINAL_FILENAME	INTERNAL_NAME
#define PRODUCT_NAME	INTERNAL_NAME
#define PRODUCT_VERSION	VER_STRING

#endif /*SAFOR_VERSION_H*/
