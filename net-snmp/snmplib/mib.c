/*
 * mib.c
 *
 * Update: 1998-07-17 <jhy@gsu.edu>
 * Added print_oid_report* functions.
 *
 */
/**********************************************************************
	Copyright 1988, 1989, 1991, 1992 by Carnegie Mellon University

                      All Rights Reserved

Permission to use, copy, modify, and distribute this software and its
documentation for any purpose and without fee is hereby granted,
provided that the above copyright notice appear in all copies and that
both that copyright notice and this permission notice appear in
supporting documentation, and that the name of CMU not be
used in advertising or publicity pertaining to distribution of the
software without specific, written prior permission.

CMU DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE, INCLUDING
ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO EVENT SHALL
CMU BE LIABLE FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR
ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION,
ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS
SOFTWARE.
******************************************************************/
#include <config.h>

#include <stdio.h>
#include <ctype.h>
#include <sys/types.h>
#if HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif
#if TIME_WITH_SYS_TIME
# ifdef WIN32
#  include <sys/timeb.h>
# else
#  include <sys/time.h>
# endif
# include <time.h>
#else
# if HAVE_SYS_TIME_H
#  include <sys/time.h>
# else
#  include <time.h>
# endif
#endif
#if HAVE_STRING_H
#include <string.h>
#else
#include <strings.h>
#endif
#if HAVE_STDLIB_H
#include <stdlib.h>
#endif
#if HAVE_SYS_SELECT_H
#include <sys/select.h>
#endif

#if HAVE_WINSOCK_H
#include <winsock.h>
#endif

#if HAVE_DMALLOC_H
#include <dmalloc.h>
#endif

#include "asn1.h"
#include "snmp_api.h"
#include "mib.h"
#include "snmp.h"
#include "snmp_impl.h"
#include "parse.h"
#include "int64.h"
#include "system.h"
#include "read_config.h"
#include "snmp_debug.h"
#include "default_store.h"

static struct tree * _sprint_objid(char *buf, oid *objid, size_t objidlen);
static char *uptimeString (u_long, char *);
static struct tree *_get_symbol(oid *objid, size_t objidlen, struct tree *subtree,
    			char *buf, struct index_list *in_dices, char **end_of_known);
  
static void print_tree_node (FILE *, struct tree *, int);

/* helper functions for get_module_node */
static int node_to_oid(struct tree *, oid *, size_t *);
static int _add_strings_to_oid(struct tree *, char *,
             oid *, size_t *, size_t);

extern struct tree *tree_head;
static struct tree *tree_top;

struct tree *Mib;             /* Backwards compatibility */

oid RFC1213_MIB[] = { 1, 3, 6, 1, 2, 1 };
static char Standard_Prefix[] = ".1.3.6.1.2.1";

/* Set default here as some uses of read_objid require valid pointer. */
static char *Prefix = &Standard_Prefix[0];
typedef struct _PrefixList {
	const char *str;
	int len;
} *PrefixListPtr, PrefixList;

/*
 * Here are the prefix strings.
 * Note that the first one finds the value of Prefix or Standard_Prefix.
 * Any of these MAY start with period; all will NOT end with period.
 * Period is added where needed.  See use of Prefix in this module.
 */
PrefixList mib_prefixes[] = {
	{ &Standard_Prefix[0] }, /* placeholder for Prefix data */
	{ ".iso.org.dod.internet.mgmt.mib-2" },
	{ ".iso.org.dod.internet.experimental" },
	{ ".iso.org.dod.internet.private" },
	{ ".iso.org.dod.internet.snmpParties" },
	{ ".iso.org.dod.internet.snmpSecrets" },
	{ NULL, 0 }  /* end of list */
};

static char *
uptimeString(u_long timeticks,
	     char *buf)
{
    int	centisecs, seconds, minutes, hours, days;

    if (ds_get_boolean(DS_LIBRARY_ID, DS_LIB_NUMERIC_TIMETICKS)) {
	sprintf(buf,"%ld",timeticks);
	return buf;
    }


    centisecs = timeticks % 100;
    timeticks /= 100;
    days = timeticks / (60 * 60 * 24);
    timeticks %= (60 * 60 * 24);

    hours = timeticks / (60 * 60);
    timeticks %= (60 * 60);

    minutes = timeticks / 60;
    seconds = timeticks % 60;

    if (ds_get_boolean(DS_LIBRARY_ID, DS_LIB_QUICK_PRINT))
	sprintf(buf, "%d:%d:%02d:%02d.%02d",
		days, hours, minutes, seconds, centisecs);
    else {
	if (days == 0){
	    sprintf(buf, "%d:%02d:%02d.%02d",
		hours, minutes, seconds, centisecs);
	} else if (days == 1) {
	    sprintf(buf, "%d day, %d:%02d:%02d.%02d",
		days, hours, minutes, seconds, centisecs);
	} else {
	    sprintf(buf, "%d days, %d:%02d:%02d.%02d",
		days, hours, minutes, seconds, centisecs);
	}
    }
    return buf;
}



/* prints character pointed to if in human-readable ASCII range,
	otherwise prints a blank space */
static void sprint_char(char *buf, const u_char ch)
{
	if (isprint(ch))
	{
		sprintf(buf, "%c", (int)ch);
	}
	else
	{
		sprintf(buf, ".");
	}
}


void sprint_hexstring(char *buf,
                      const u_char *cp,
                      size_t len)
{
	const u_char *tp;
	size_t lenleft;
	
    for(; len >= 16; len -= 16){
	sprintf(buf, "%02X %02X %02X %02X %02X %02X %02X %02X ", cp[0], cp[1], cp[2], cp[3], cp[4], cp[5], cp[6], cp[7]);
	buf += strlen(buf);
	cp += 8;
	sprintf(buf, "%02X %02X %02X %02X %02X %02X %02X %02X", cp[0], cp[1], cp[2], cp[3], cp[4], cp[5], cp[6], cp[7]);
	buf += strlen(buf);
	cp += 8;
	if (ds_get_boolean(DS_LIBRARY_ID, DS_LIB_PRINT_HEX_TEXT))
	{
		sprintf(buf, "  [");
		buf += strlen(buf);
		for (tp = cp - 16; tp < cp; tp ++)
		{
			sprint_char(buf++, *tp);
		}
		sprintf(buf, "]");
		buf += strlen(buf);
	}
	if (len > 16) { *buf++ = '\n'; *buf = 0; }
    }
    lenleft = len;
    for(; len > 0; len--){
	sprintf(buf, "%02X ", *cp++);
	buf += strlen(buf);
    }
	if ((lenleft > 0) && ds_get_boolean(DS_LIBRARY_ID, DS_LIB_PRINT_HEX_TEXT))
	{
		sprintf(buf, " [");
		buf += strlen(buf);
		for (tp = cp - lenleft; tp < cp; tp ++)
		{
			sprint_char(buf++, *tp);
		}
		sprintf(buf, "]");
		buf += strlen(buf);
    }
    *buf = '\0';
}

void sprint_asciistring(char *buf,
		        const u_char  *cp,
		        size_t	    len)
{
    int	x;

    for(x = 0; x < (int)len; x++){
	if (isprint(*cp)){
	    if (*cp == '\\' || *cp == '"')
		*buf++ = '\\';
	    *buf++ = *cp++;
	} else {
	    *buf++ = '.';
	    cp++;
	}
    }
    *buf = '\0';
}


/*
  0
  < 4
  hex

  0 ""
  < 4 hex Hex: oo oo oo
  < 4     "fgh" Hex: oo oo oo
  > 4 hex Hex: oo oo oo oo oo oo oo oo
  > 4     "this is a test"

  */

void
sprint_octet_string(char *buf,
		    struct variable_list *var,
		    struct enum_list *enums,
		    const char *hint,
		    const char *units)
{
    int hex, x;
    u_char *cp;
    const char *saved_hint = hint;
    char *saved_buf = buf;

    if (var->type != ASN_OCTET_STR){
	sprintf(buf, "Wrong Type (should be OCTET STRING): ");
	buf += strlen(buf);
	sprint_by_type(buf, var, NULL, NULL, NULL);
	return;
    }

    if (hint) {
	int repeat, width = 1;
	long value;
	char code = 'd', separ = 0, term = 0, ch;
	u_char *ecp;

	*buf = 0;
	cp = var->val.string;
	ecp = cp + var->val_len;
	while (cp < ecp) {
	    repeat = 1;
	    if (*hint) {
		if (*hint == '*') {
		    repeat = *cp++;
		    hint++;
		}
		width = 0;
		while ('0' <= *hint && *hint <= '9')
		    width = width * 10 + *hint++ - '0';
		code = *hint++;
		if ((ch = *hint) && ch != '*' && (ch < '0' || ch > '9')
                    && (width != 0 || (ch != 'x' && ch != 'd' && ch != 'o')))
		    separ = *hint++;
		else separ = 0;
		if ((ch = *hint) && ch != '*' && (ch < '0' || ch > '9')
                    && (width != 0 || (ch != 'x' && ch != 'd' && ch != 'o')))
		    term = *hint++;
		else term = 0;
		if (width == 0) width = 1;
	    }
	    while (repeat && cp < ecp) {
                value = 0;
		if (code != 'a')
		    for (x = 0; x < width; x++) value = value * 256 + *cp++;
		switch (code) {
		case 'x':
                    sprintf (buf, "%lx", value); break;
		case 'd':
                    sprintf (buf, "%ld", value); break;
		case 'o':
                    sprintf (buf, "%lo", value); break;
		case 'a':
                    for (x = 0; x < width && cp < ecp; x++)
			*buf++ = *cp++;
		    *buf = 0;
		    break;
		default:
		    sprintf(saved_buf, "(Bad hint ignored: %s) ", saved_hint);
		    sprint_octet_string(saved_buf+strlen(saved_buf),
					var, enums, NULL, NULL);
		    return;
		}
		buf += strlen (buf);
		if (cp < ecp && separ) *buf++ = separ;
		repeat--;
	    }
	    if (term && cp < ecp) *buf++ = term;
	}
	if (units) sprintf (buf, " %s", units);
        return;
    }

    hex = 0;
    for(cp = var->val.string, x = 0; x < (int)var->val_len; x++, cp++){
	if (!(isprint(*cp) || isspace(*cp)))
	    hex = 1;
    }
    if (var->val_len == 0){
	strcpy(buf, "\"\"");
	return;
    }
    if (!hex){
	*buf++ = '"';
	sprint_asciistring(buf, var->val.string, var->val_len);
	buf += strlen(buf);
	*buf++ = '"';
	*buf = '\0';
    }
    if (hex || ((var->val_len <= 4) && !ds_get_boolean(DS_LIBRARY_ID, DS_LIB_QUICK_PRINT))){
	if (ds_get_boolean(DS_LIBRARY_ID, DS_LIB_QUICK_PRINT)){
	    *buf++ = '"';
	    *buf = '\0';
	} else {
	    sprintf(buf, " Hex: ");
	    buf += strlen(buf);
	}
	sprint_hexstring(buf, var->val.string, var->val_len);
	if (ds_get_boolean(DS_LIBRARY_ID, DS_LIB_QUICK_PRINT)){
	    buf += strlen(buf);
	    *buf++ = '"';
	    *buf = '\0';
	}
    }
    if (units) sprintf (buf, " %s", units);
}

#ifdef OPAQUE_SPECIAL_TYPES

void
sprint_float(char *buf,
	     struct variable_list *var,
	     struct enum_list *enums,
	     const char *hint,
	     const char *units)
{
  if (var->type != ASN_OPAQUE_FLOAT) {
	sprintf(buf, "Wrong Type (should be Float): ");
	buf += strlen(buf);
	sprint_by_type(buf, var, NULL, NULL, NULL);
	return;
    }
    if (!ds_get_boolean(DS_LIBRARY_ID, DS_LIB_QUICK_PRINT)){
	sprintf(buf, "Opaque: Float:");
	buf += strlen(buf);
    }
    sprintf(buf, " %f", *var->val.floatVal);
    buf += strlen (buf);
    if (units) sprintf (buf, " %s", units);
}

void
sprint_double(char *buf,
	      struct variable_list *var,
	      struct enum_list *enums,
	      const char *hint,
	      const char *units)
{
  if (var->type != ASN_OPAQUE_DOUBLE) {
	sprintf(buf, "Wrong Type (should be Double): ");
	buf += strlen(buf);
	sprint_by_type(buf, var, NULL, NULL, NULL);
	return;
    }
    if (!ds_get_boolean(DS_LIBRARY_ID, DS_LIB_QUICK_PRINT)){
	sprintf(buf, "Opaque: Double:");
	buf += strlen(buf);
    }
    sprintf(buf, " %f", *var->val.doubleVal);
    buf += strlen (buf);
    if (units) sprintf (buf, " %s", units);
}

#endif /* OPAQUE_SPECIAL_TYPES */

void
sprint_opaque(char *buf,
	      struct variable_list *var,
	      struct enum_list *enums,
	      const char *hint,
	      const char *units)
{

    if (var->type != ASN_OPAQUE
#ifdef OPAQUE_SPECIAL_TYPES
        && var->type != ASN_OPAQUE_COUNTER64
        && var->type != ASN_OPAQUE_U64
        && var->type != ASN_OPAQUE_I64
        && var->type != ASN_OPAQUE_FLOAT
        && var->type != ASN_OPAQUE_DOUBLE
#endif /* OPAQUE_SPECIAL_TYPES */
      ){
	sprintf(buf, "Wrong Type (should be Opaque): ");
	buf += strlen(buf);
	sprint_by_type(buf, var, NULL, NULL, NULL);
	return;
    }
#ifdef OPAQUE_SPECIAL_TYPES
    switch(var->type) {
      case ASN_OPAQUE_COUNTER64:
      case ASN_OPAQUE_U64:
      case ASN_OPAQUE_I64:
        sprint_counter64(buf, var, enums, hint, units);
        break;

      case ASN_OPAQUE_FLOAT:
        sprint_float(buf, var, enums, hint, units);
        break;

      case ASN_OPAQUE_DOUBLE:
        sprint_double(buf, var, enums, hint, units);
        break;

      case ASN_OPAQUE:
#endif
    if (!ds_get_boolean(DS_LIBRARY_ID, DS_LIB_QUICK_PRINT)){
	sprintf(buf, "OPAQUE: ");
	buf += strlen(buf);
    }
    sprint_hexstring(buf, var->val.string, var->val_len);
    buf += strlen (buf);
#ifdef OPAQUE_SPECIAL_TYPES
    }
#endif
    if (units) sprintf (buf, " %s", units);
}

void
sprint_object_identifier(char *buf,
			 struct variable_list *var,
			 struct enum_list *enums,
			 const char *hint,
			 const char *units)
{
    if (var->type != ASN_OBJECT_ID){
	sprintf(buf, "Wrong Type (should be OBJECT IDENTIFIER): ");
	buf += strlen(buf);
	sprint_by_type(buf, var, NULL, NULL, NULL);
	return;
    }
    if (!ds_get_boolean(DS_LIBRARY_ID, DS_LIB_QUICK_PRINT)){
	sprintf(buf, "OID: ");
	buf += strlen(buf);
    }
    _sprint_objid(buf, (oid *)(var->val.objid), var->val_len / sizeof(oid));
    buf += strlen (buf);
    if (units) sprintf (buf, " %s", units);
}

void
sprint_timeticks(char *buf,
		 struct variable_list *var,
		 struct enum_list *enums,
		 const char *hint,
		 const char *units)
{
    char timebuf[32];

    if (var->type != ASN_TIMETICKS){
	sprintf(buf, "Wrong Type (should be Timeticks): ");
	buf += strlen(buf);
	sprint_by_type(buf, var, NULL, NULL, NULL);
	return;
    }
    if (ds_get_boolean(DS_LIBRARY_ID, DS_LIB_NUMERIC_TIMETICKS)) {
        sprintf(buf,"%lu", *(u_long *)(var->val.integer));
        return;
    }
    if (!ds_get_boolean(DS_LIBRARY_ID, DS_LIB_QUICK_PRINT)){
	sprintf(buf, "Timeticks: (%lu) ", *(u_long *)(var->val.integer));
	buf += strlen(buf);
    }
    sprintf(buf, "%s", uptimeString(*(u_long *)(var->val.integer), timebuf));
    buf += strlen (buf);
    if (units) sprintf (buf, " %s", units);
}

void
sprint_hinted_integer (char *buf,
		       long val,
		       const char *hint,
		       const char *units)
{
    char code;
    int shift, len;
    char tmp[256];
    char fmt[10];

    code = hint[0];
    if (hint [1] == '-') {
        shift = atoi (hint+2);
    }
    else shift = 0;
    fmt[0] = '%';
    fmt[1] = 'l';
    fmt[2] = code;
    fmt[3] = 0;
    sprintf (tmp, fmt, val);
    if (shift != 0) {
	len = strlen (tmp);
	if (shift <= len) {
	    tmp[len+1] = 0;
	    while (shift--) {
		tmp[len] = tmp[len-1];
		len--;
	    }
	    tmp[len] = '.';
	}
	else {
	    tmp[shift+1] = 0;
	    while (shift) {
		if (len-- > 0) tmp [shift] = tmp [len];
		else tmp[shift] = '0';
		shift--;
	    }
	    tmp[0] = '.';
	}
    }
    strcpy (buf, tmp);
}

void
sprint_integer(char *buf,
	       struct variable_list *var,
	       struct enum_list *enums,
	       const char *hint,
	       const char *units)
{
    char    *enum_string = NULL;

    if (var->type != ASN_INTEGER){
	sprintf(buf, "Wrong Type (should be INTEGER): ");
	buf += strlen(buf);
	sprint_by_type(buf, var, NULL, NULL, NULL);
	return;
    }
    for (; enums; enums = enums->next)
	if (enums->value == *var->val.integer){
	    enum_string = enums->label;
	    break;
	}
    if (enum_string == NULL ||
        ds_get_boolean(DS_LIBRARY_ID,DS_LIB_PRINT_NUMERIC_ENUM)) {
	if (hint) sprint_hinted_integer(buf, *var->val.integer, hint, units);
	else sprintf(buf, "%ld", *var->val.integer);
    }
    else if (ds_get_boolean(DS_LIBRARY_ID, DS_LIB_QUICK_PRINT))
	sprintf(buf, "%s", enum_string);
    else
	sprintf(buf, "%s(%ld)", enum_string, *var->val.integer);
    buf += strlen (buf);
    if (units) sprintf (buf, " %s", units);
}

void
sprint_uinteger(char *buf,
		struct variable_list *var,
		struct enum_list *enums,
		const char *hint,
		const char *units)
{
    char    *enum_string = NULL;

    if (var->type != ASN_UINTEGER){
	sprintf(buf, "Wrong Type (should be UInteger32): ");
	buf += strlen(buf);
	sprint_by_type(buf, var, NULL, NULL, NULL);
	return;
    }
    for (; enums; enums = enums->next)
	if (enums->value == *var->val.integer){
	    enum_string = enums->label;
	    break;
	}
    if (enum_string == NULL ||
        ds_get_boolean(DS_LIBRARY_ID,DS_LIB_PRINT_NUMERIC_ENUM))
	sprintf(buf, "%lu", *var->val.integer);
    else if (ds_get_boolean(DS_LIBRARY_ID, DS_LIB_QUICK_PRINT))
	sprintf(buf, "%s", enum_string);
    else
	sprintf(buf, "%s(%lu)", enum_string, *var->val.integer);
    buf += strlen (buf);
    if (units) sprintf (buf, " %s", units);
}

void
sprint_gauge(char *buf,
	     struct variable_list *var,
	     struct enum_list *enums,
	     const char *hint,
	     const char *units)
{
    if (var->type != ASN_GAUGE){
	sprintf(buf, "Wrong Type (should be Gauge32 or Unsigned32): ");
	buf += strlen(buf);
	sprint_by_type(buf, var, NULL, NULL, NULL);
	return;
    }
    if (ds_get_boolean(DS_LIBRARY_ID, DS_LIB_QUICK_PRINT))
	sprintf(buf, "%lu", *var->val.integer);
    else
	sprintf(buf, "Gauge32: %lu", *var->val.integer);
    buf += strlen (buf);
    if (units) sprintf (buf, " %s", units);
}

void
sprint_counter(char *buf,
	       struct variable_list *var,
	       struct enum_list *enums,
	       const char *hint,
	       const char *units)
{
    if (var->type != ASN_COUNTER){
	sprintf(buf, "Wrong Type (should be Counter32): ");
	buf += strlen(buf);
	sprint_by_type(buf, var, NULL, NULL, NULL);
	return;
    }
    if (ds_get_boolean(DS_LIBRARY_ID, DS_LIB_QUICK_PRINT))
	sprintf(buf, "%lu", *var->val.integer);
    else
	sprintf(buf, "Counter32: %lu", *var->val.integer);
    buf += strlen (buf);
    if (units) sprintf (buf, " %s", units);
}

void
sprint_networkaddress(char *buf,
		      struct variable_list *var,
		      struct enum_list *enums,
		      const char *hint,
		      const char *units)
{
    int x, len;
    u_char *cp;

    if (var->type != ASN_IPADDRESS){
	sprintf(buf, "Wrong Type (should be NetworkAddress): ");
	buf += strlen(buf);
	sprint_by_type(buf, var, NULL, NULL, NULL);
	return;
    }
    if (!ds_get_boolean(DS_LIBRARY_ID, DS_LIB_QUICK_PRINT)){
	sprintf(buf, "Network Address: ");
	buf += strlen(buf);
    }
    cp = var->val.string;
    len = var->val_len;
    for(x = 0; x < len; x++){
	sprintf(buf, "%02X", *cp++);
	buf += strlen(buf);
	if (x < (len - 1))
	    *buf++ = ':';
    }
}

void
sprint_ipaddress(char *buf,
		 struct variable_list *var,
		 struct enum_list *enums,
		 const char *hint,
		 const char *units)
{
    u_char *ip;

    if (var->type != ASN_IPADDRESS){
	sprintf(buf, "Wrong Type (should be IpAddress): ");
	buf += strlen(buf);
	sprint_by_type(buf, var, NULL, NULL, NULL);
	return;
    }
    ip = var->val.string;
    if (ds_get_boolean(DS_LIBRARY_ID, DS_LIB_QUICK_PRINT))
	sprintf(buf, "%d.%d.%d.%d",ip[0], ip[1], ip[2], ip[3]);
    else
	sprintf(buf, "IpAddress: %d.%d.%d.%d",ip[0], ip[1], ip[2], ip[3]);
}

void
sprint_null(char *buf,
	    struct variable_list *var,
	    struct enum_list *enums,
	    const char *hint,
	    const char *units)
{
    if (var->type != ASN_NULL){
	sprintf(buf, "Wrong Type (should be NULL): ");
	buf += strlen(buf);
	sprint_by_type(buf, var, NULL, NULL, NULL);
	return;
    }
    sprintf(buf, "NULL");
}

void
sprint_bitstring(char *buf,
		 struct variable_list *var,
		 struct enum_list *enums,
		 const char *hint,
		 const char *units)
{
    int len, bit;
    u_char *cp;
    char *enum_string;

    if (var->type != ASN_BIT_STR && var->type != ASN_OCTET_STR){
	sprintf(buf, "Wrong Type (should be BITS): ");
	buf += strlen(buf);
	sprint_by_type(buf, var, NULL, NULL, NULL);
	return;
    }
    if (ds_get_boolean(DS_LIBRARY_ID, DS_LIB_QUICK_PRINT)){
	*buf++ = '"';
	*buf = '\0';
    } else {
	sprintf(buf, "BITS: ");
	buf += strlen(buf);
    }
    sprint_hexstring(buf, var->val.bitstring, var->val_len);
    buf += strlen(buf);

    if (ds_get_boolean(DS_LIBRARY_ID, DS_LIB_QUICK_PRINT)){
	buf += strlen(buf);
	*buf++ = '"';
	*buf = '\0';
    } else {
	cp = var->val.bitstring;
	for(len = 0; len < (int)var->val_len; len++){
	    for(bit = 0; bit < 8; bit++){
		if (*cp & (0x80 >> bit)){
		    enum_string = NULL;
		    for (; enums; enums = enums->next)
			if (enums->value == (len * 8) + bit){
			    enum_string = enums->label;
			    break;
			}
		    if (enum_string == NULL ||
                        ds_get_boolean(DS_LIBRARY_ID,DS_LIB_PRINT_NUMERIC_ENUM))
			sprintf(buf, "%d ", (len * 8) + bit);
		    else
			sprintf(buf, "%s(%d) ", enum_string, (len * 8) + bit);
		    buf += strlen(buf);
		}
	    }
	    cp ++;
	}
    }
}

void
sprint_nsapaddress(char *buf,
		   struct variable_list *var,
		   struct enum_list *enums,
		   const char *hint,
		   const char *units)
{
    if (var->type != ASN_NSAP){
	sprintf(buf, "Wrong Type (should be NsapAddress): ");
	buf += strlen(buf);
	sprint_by_type(buf, var, NULL, NULL, NULL);
	return;
    }
    if (!ds_get_boolean(DS_LIBRARY_ID, DS_LIB_QUICK_PRINT)){
	sprintf(buf, "NsapAddress: ");
	buf += strlen(buf);
    }
    sprint_hexstring(buf, var->val.string, var->val_len);
}

void
sprint_counter64(char *buf,
		 struct variable_list *var,
		 struct enum_list *enums,
		 const char *hint,
		 const char *units)
{
    char a64buf[I64CHARSZ+1];

  if (var->type != ASN_COUNTER64
#ifdef OPAQUE_SPECIAL_TYPES
      && var->type != ASN_OPAQUE_COUNTER64
      && var->type != ASN_OPAQUE_I64
      && var->type != ASN_OPAQUE_U64
#endif
    ){
	sprintf(buf, "Wrong Type (should be Counter64): ");
	buf += strlen(buf);
	sprint_by_type(buf, var, NULL, NULL, NULL);
	return;
    }
    if (!ds_get_boolean(DS_LIBRARY_ID, DS_LIB_QUICK_PRINT)){
#ifdef OPAQUE_SPECIAL_TYPES
      if (var->type != ASN_COUNTER64) {
	sprintf(buf, "Opaque: ");
	buf += strlen(buf);
      }
#endif
#ifdef OPAQUE_SPECIAL_TYPES
        switch(var->type) {
          case ASN_OPAQUE_U64:
            sprintf(buf, "UInt64: ");
            break;
          case ASN_OPAQUE_I64:
            sprintf(buf, "Int64: ");
            break;
          case ASN_COUNTER64:
          case ASN_OPAQUE_COUNTER64:
#endif
            sprintf(buf, "Counter64: ");
#ifdef OPAQUE_SPECIAL_TYPES
        }
#endif
	buf += strlen(buf);
    }
#ifdef OPAQUE_SPECIAL_TYPES
    if (var->type == ASN_OPAQUE_I64)
    {
      printI64(a64buf, var->val.counter64);
      sprintf(buf, a64buf);
    }
    else
#endif
    {
      printU64(a64buf, var->val.counter64);
      sprintf(buf, a64buf);
    }
    buf += strlen (buf);
    if (units) sprintf (buf, " %s", units);
}

void
sprint_unknowntype(char *buf,
		   struct variable_list *var,
		   struct enum_list *enums,
		   const char *hint,
		   const char *units)
{
/*    sprintf(buf, "Variable has bad type"); */
    sprint_by_type(buf, var, NULL, NULL, NULL);
}

void
sprint_badtype(char *buf,
	       struct variable_list *var,
	       struct enum_list *enums,
	       const char *hint,
	       const char *units)
{
    sprintf(buf, "Variable has bad type");
}

void
sprint_by_type(char *buf,
	       struct variable_list *var,
	       struct enum_list *enums,
	       const char *hint,
	       const char *units)
{
    switch (var->type){
	case ASN_INTEGER:
	    sprint_integer(buf, var, enums, hint, units);
	    break;
	case ASN_OCTET_STR:
	    sprint_octet_string(buf, var, enums, hint, units);
	    break;
	case ASN_BIT_STR:
	    sprint_bitstring(buf, var, enums, hint, units);
	    break;
	case ASN_OPAQUE:
	    sprint_opaque(buf, var, enums, hint, units);
	    break;
	case ASN_OBJECT_ID:
	    sprint_object_identifier(buf, var, enums, hint, units);
	    break;
	case ASN_TIMETICKS:
	    sprint_timeticks(buf, var, enums, hint, units);
	    break;
	case ASN_GAUGE:
	    sprint_gauge(buf, var, enums, hint, units);
	    break;
	case ASN_COUNTER:
	    sprint_counter(buf, var, enums, hint, units);
	    break;
	case ASN_IPADDRESS:
	    sprint_ipaddress(buf, var, enums, hint, units);
	    break;
	case ASN_NULL:
	    sprint_null(buf, var, enums, hint, units);
	    break;
	case ASN_UINTEGER:
	    sprint_uinteger(buf, var, enums, hint, units);
	    break;
	case ASN_COUNTER64:
#ifdef OPAQUE_SPECIAL_TYPES
	case ASN_OPAQUE_U64:
	case ASN_OPAQUE_I64:
	case ASN_OPAQUE_COUNTER64:
#endif /* OPAQUE_SPECIAL_TYPES */
	    sprint_counter64(buf, var, enums, hint, units);
	    break;
#ifdef OPAQUE_SPECIAL_TYPES
	case ASN_OPAQUE_FLOAT:
	    sprint_float(buf, var, enums, hint, units);
	    break;
	case ASN_OPAQUE_DOUBLE:
	    sprint_double(buf, var, enums, hint, units);
	    break;
#endif /* OPAQUE_SPECIAL_TYPES */
	default:
            DEBUGMSGTL(("sprint_by_type", "bad type: %d\n", var->type));
	    sprint_badtype(buf, var, enums, hint, units);
	    break;
    }
}


struct tree *get_tree_head(void)
{
   return(tree_head);
}

static char *confmibdir=NULL;
static char *confmibs=NULL;

void
handle_mibdirs_conf(const char *token,
		    char *line)
{
    char *ctmp;

    if (confmibdir) {
        ctmp = (char *)malloc(strlen(confmibdir) + strlen(line) + 1);
        if (*line == '+')
            line++;
        sprintf(ctmp,"%s%c%s",confmibdir, ENV_SEPARATOR_CHAR, line);
        free(confmibdir);
        confmibdir = ctmp;
    } else {
        confmibdir=strdup(line);
    }
    DEBUGMSGTL(("read_config:initmib", "using mibdirs: %s\n", confmibdir));
}

void
handle_mibs_conf(const char *token,
		 char *line)
{
    char *ctmp;

    if (confmibs) {
        ctmp = (char *)malloc(strlen(confmibs) + strlen(line) + 1);
        if (*line == '+')
            line++;
        sprintf(ctmp,"%s%c%s",confmibs, ENV_SEPARATOR_CHAR, line);
        free(confmibs);
        confmibs = ctmp;
    } else {
        confmibs=strdup(line);
    }
    DEBUGMSGTL(("read_config:initmib", "using mibs: %s\n", confmibs));
}

void
handle_mibfile_conf(const char *token,
		    char *line)
{
  DEBUGMSGTL(("read_config:initmib", "reading mibfile: %s\n", line));
  read_mib(line);
}

char *
snmp_out_toggle_options(char *options)
{
    while(*options) {
        switch(*options++) {
        case 'n':
            ds_toggle_boolean(DS_LIBRARY_ID, DS_LIB_PRINT_NUMERIC_OIDS);
            break;
        case 'e':
            ds_toggle_boolean(DS_LIBRARY_ID, DS_LIB_PRINT_NUMERIC_ENUM);
            break;
        case 'b':
            ds_toggle_boolean(DS_LIBRARY_ID, DS_LIB_DONT_BREAKDOWN_OIDS);
            break;
	case 'E':
	    ds_toggle_boolean(DS_LIBRARY_ID, DS_LIB_ESCAPE_QUOTES);
	    break;
	case 'X':
	    ds_toggle_boolean(DS_LIBRARY_ID, DS_LIB_EXTENDED_INDEX);
	    break;
	case 'q':
	    ds_toggle_boolean(DS_LIBRARY_ID, DS_LIB_QUICK_PRINT);
	    break;
        case 'f':
            ds_toggle_boolean(DS_LIBRARY_ID, DS_LIB_PRINT_FULL_OID);
	    break;
	case 't':
	    ds_toggle_boolean(DS_LIBRARY_ID, DS_LIB_NUMERIC_TIMETICKS);
	    break;
	case 'v':
	    ds_toggle_boolean(DS_LIBRARY_ID, DS_LIB_PRINT_BARE_VALUE);
	    break;
        case 's':
	    snmp_set_suffix_only(1);
	    break;
        case 'S':
	    snmp_set_suffix_only(2);
	    break;
	     case 'T':
	     ds_toggle_boolean(DS_LIBRARY_ID, DS_LIB_PRINT_HEX_TEXT);
	     break;
        default:
	    return options-1;
	}
    }
    return NULL;
}

void snmp_out_toggle_options_usage(const char *lead, FILE *outf)
{
  fprintf(outf, "%sOUTOPTS values:\n", lead);
  fprintf(outf, "%s    n: Print oids numerically.\n", lead);
  fprintf(outf, "%s    e: Print enums numerically.\n", lead);
  fprintf(outf, "%s    E: Escape quotes in string indices.\n", lead);
  fprintf(outf, "%s    X: Extended index format\n", lead);
  fprintf(outf, "%s    b: Dont break oid indexes down.\n", lead);
  fprintf(outf, "%s    q: Quick print for easier parsing.\n", lead);
  fprintf(outf, "%s    f: Print full oids on output.\n", lead);
  fprintf(outf, "%s    s: Print only last symbolic element of oid.\n", lead);
  fprintf(outf, "%s    S: Print MIB module-id plus last element.\n", lead);
  fprintf(outf, "%s    t: Print timeticks unparsed as numeric integers.\n", lead);
  fprintf(outf, "%s    v: Print Print values only (not OID = value).\n", lead);
  fprintf(outf, "%s    T: Print human-readable text along with hex strings.\n", lead);
}

char *
snmp_in_toggle_options(char *options)
{
    while(*options) {
        switch(*options++) {
	case 'R':
	    ds_toggle_boolean(DS_LIBRARY_ID, DS_LIB_RANDOM_ACCESS);
	    break;
	case 'b':
	    ds_toggle_boolean(DS_LIBRARY_ID, DS_LIB_REGEX_ACCESS);
	    break;
	case 'r':
	    ds_toggle_boolean(DS_LIBRARY_ID, DS_LIB_DONT_CHECK_RANGE);
	    break;
        default:
	    return options-1;
	}
    }
    return NULL;
}

void snmp_in_toggle_options_usage(const char *lead, FILE *outf)
{
  fprintf(outf, "%sINOPTS values:\n", lead);
  fprintf(outf, "%s    R: Do random access to oid labels.\n", lead);
  fprintf(outf, "%s    r: Don't check values for range/type legality.\n", lead);
  fprintf(outf, "%s    b: Do best/regex matching to find a MIB node.\n", lead);
}

void
register_mib_handlers (void)
{
    register_premib_handler("snmp","mibdirs",
			    handle_mibdirs_conf, NULL,
			    "[mib-dirs|+mib-dirs]");
    register_premib_handler("snmp","mibs",
			    handle_mibs_conf,NULL,
			    "[mib-tokens|+mib-tokens]");
    register_config_handler("snmp","mibfile",
			    handle_mibfile_conf, NULL,
			    "mibfile-to-read");

    /* register the snmp.conf configuration handlers for default
       parsing behaviour */

    ds_register_premib(ASN_BOOLEAN, "snmp","showMibErrors",
                       DS_LIBRARY_ID, DS_LIB_MIB_ERRORS);
    ds_register_premib(ASN_BOOLEAN, "snmp","strictCommentTerm",
                       DS_LIBRARY_ID, DS_LIB_MIB_COMMENT_TERM);
    ds_register_premib(ASN_BOOLEAN, "snmp","mibAllowUnderline",
                       DS_LIBRARY_ID, DS_LIB_MIB_PARSE_LABEL);
    ds_register_premib(ASN_INTEGER, "snmp","mibWarningLevel",
                       DS_LIBRARY_ID, DS_LIB_MIB_WARNINGS);
    ds_register_premib(ASN_BOOLEAN, "snmp","mibReplaceWithLatest",
                       DS_LIBRARY_ID, DS_LIB_MIB_REPLACE);

    ds_register_premib(ASN_BOOLEAN, "snmp","printNumericEnums",
                       DS_LIBRARY_ID, DS_LIB_PRINT_NUMERIC_ENUM);
    ds_register_premib(ASN_BOOLEAN, "snmp","printNumericOids",
                       DS_LIBRARY_ID, DS_LIB_PRINT_NUMERIC_OIDS);
    ds_register_premib(ASN_BOOLEAN, "snmp","escapeQuotes",
		       DS_LIBRARY_ID, DS_LIB_ESCAPE_QUOTES);
    ds_register_premib(ASN_BOOLEAN, "snmp","dontBreakdownOids",
                       DS_LIBRARY_ID, DS_LIB_DONT_BREAKDOWN_OIDS);
    ds_register_premib(ASN_BOOLEAN, "snmp","quickPrinting",
                       DS_LIBRARY_ID, DS_LIB_QUICK_PRINT);
    ds_register_premib(ASN_BOOLEAN, "snmp","numericTimeticks",
		       DS_LIBRARY_ID, DS_LIB_NUMERIC_TIMETICKS);
    ds_register_premib(ASN_INTEGER, "snmp","suffixPrinting",
                       DS_LIBRARY_ID, DS_LIB_PRINT_SUFFIX_ONLY);
    ds_register_premib(ASN_BOOLEAN, "snmp","extendedIndex",
		       DS_LIBRARY_ID, DS_LIB_EXTENDED_INDEX);
    ds_register_premib(ASN_BOOLEAN, "snmp","printHexText",
		       DS_LIBRARY_ID, DS_LIB_PRINT_HEX_TEXT);
}

void
init_mib (void)
{
    const char *prefix;
    char  *env_var, *entry;
    PrefixListPtr pp = &mib_prefixes[0];
    char *new_mibdirs, *homepath, *cp_home;

    if (Mib) return;
    init_mib_internals();

    /* Initialise the MIB directory/ies */

    /* we can't use the environment variable directly, because strtok
       will modify it. */

    env_var = getenv("MIBDIRS");
    if ( env_var == NULL ) {
	if (confmibdir != NULL)
	    env_var = strdup(confmibdir);
	else
	    env_var = strdup(DEFAULT_MIBDIRS);
    } else {
	env_var = strdup(env_var);
    }
    if (*env_var == '+') {
	entry = (char *)malloc(strlen(DEFAULT_MIBDIRS)+strlen(env_var)+2);
	sprintf(entry, "%s%c%s", DEFAULT_MIBDIRS, ENV_SEPARATOR_CHAR, env_var+1);
	free(env_var);
	env_var = entry;
    }

    /* replace $HOME in the path with the users home directory */
    homepath=getenv("HOME");

    if (homepath) {
      while((cp_home = strstr(env_var, "$HOME"))) {
        new_mibdirs = (char *) malloc(strlen(env_var) - strlen("$HOME") +
                                      strlen(homepath)+1);
        *cp_home = 0; /* null out the spot where we stop copying */
        sprintf(new_mibdirs, "%s%s%s", env_var, homepath,
                cp_home + strlen("$HOME"));
        /* swap in the new value and repeat */
        free(env_var);
        env_var = new_mibdirs;
      }
    }

    DEBUGMSGTL(("init_mib","Seen MIBDIRS: Looking in '%s' for mib dirs ...\n",env_var));

    entry = strtok( env_var, ENV_SEPARATOR );
    while ( entry ) {
        add_mibdir(entry);
        entry = strtok( NULL, ENV_SEPARATOR);
    }
    free(env_var);

    init_mib_internals();

    /* Read in any modules or mibs requested */

    env_var = getenv("MIBS");
    if ( env_var == NULL ) {
	if (confmibs != NULL)
        env_var = strdup(confmibs);
	else
	    env_var = strdup(DEFAULT_MIBS);
    } else {
	env_var = strdup(env_var);
    }
    if (*env_var == '+') {
	entry = (char *)malloc(strlen(DEFAULT_MIBS)+strlen(env_var)+2);
	sprintf(entry, "%s%c%s", DEFAULT_MIBS, ENV_SEPARATOR_CHAR, env_var+1);
	free(env_var);
	env_var = entry;
    }

    DEBUGMSGTL(("init_mib","Seen MIBS: Looking in '%s' for mib files ...\n",env_var));
    entry = strtok( env_var, ENV_SEPARATOR );
    while ( entry ) {
        if (strcasecmp(entry, DEBUG_ALWAYS_TOKEN) == 0) {
            read_all_mibs();
        }
        else if (strstr (entry, "/") != 0) {
            read_mib(entry);
        }
        else {
            read_module(entry);
        }
	    entry = strtok( NULL, ENV_SEPARATOR);
    }
    adopt_orphans();
    free(env_var);

    env_var = getenv("MIBFILES");
    if ( env_var != NULL ) {
	if (*env_var == '+') {
#ifdef DEFAULT_MIBFILES
	    entry = (char *)malloc(strlen(DEFAULT_MIBFILES)+strlen(env_var)+2);
	    sprintf(entry, "%s%c%s", DEFAULT_MIBFILES, ENV_SEPARATOR_CHAR,
		    env_var+1);
	    free(env_var);
	    env_var = entry;
#else
	    env_var = strdup(env_var+1);
#endif
	} else {
	    env_var = strdup(env_var);
	}
    } else {
#ifdef DEFAULT_MIBFILES
	env_var = strdup(DEFAULT_MIBFILES);
#endif
    }

    if ( env_var != 0 ) {
	DEBUGMSGTL(("init_mib","Seen MIBFILES: Looking in '%s' for mib files ...\n",env_var));
	entry = strtok( env_var, ENV_SEPARATOR );
	while ( entry ) {
	    read_mib(entry);
	    entry = strtok( NULL, ENV_SEPARATOR);
	}
	free(env_var);
    }

    prefix = getenv("PREFIX");

    if (!prefix)
        prefix = Standard_Prefix;

    Prefix = (char*)malloc(strlen(prefix)+2);
    strcpy(Prefix, prefix);

    DEBUGMSGTL(("init_mib","Seen PREFIX: Looking in '%s' for prefix ...\n", Prefix));

    /* remove trailing dot */
    env_var = &Prefix[strlen(Prefix) - 1];
    if (*env_var == '.') *env_var = '\0';

    pp->str = Prefix;	/* fixup first mib_prefix entry */
    /* now that the list of prefixes is built, save each string length. */
    while (pp->str) {
	pp->len = strlen(pp->str);
	pp++;
    }

    if (getenv("SUFFIX"))
	ds_set_boolean(DS_LIBRARY_ID, DS_LIB_PRINT_SUFFIX_ONLY, 1);

    Mib = tree_head;          /* Backwards compatibility */
    tree_top = (struct tree *)calloc(1,sizeof(struct tree));
    /* XX error check ? */
    if (tree_top) {
        tree_top->label = strdup("(top)");
        tree_top->child_list = tree_head;
    }
}

void
shutdown_mib (void)
{
    unload_all_mibs();
    if (tree_top) {
        if (tree_top->label) free(tree_top->label);
	free(tree_top); tree_top = NULL;
    }
    tree_head = NULL;
    Mib = NULL;
    if (Prefix != NULL && Prefix != &Standard_Prefix[0])
	free(Prefix);
    if (Prefix)
	Prefix = NULL;
}

void
print_mib (FILE *fp)
{
    print_subtree (fp, tree_head, 0);
}

void
print_ascii_dump (FILE *fp)
{
  fprintf(fp, "dump DEFINITIONS ::= BEGIN\n");
  print_ascii_dump_tree (fp, tree_head, 0);
  fprintf(fp, "END\n");
}

void
set_function(struct tree *subtree)
{
    switch(subtree->type){
	case TYPE_OBJID:
	    subtree->printer = sprint_object_identifier;
	    break;
	    case TYPE_OCTETSTR:
		subtree->printer = sprint_octet_string;
		break;
	    case TYPE_INTEGER:
		subtree->printer = sprint_integer;
		break;
	    case TYPE_INTEGER32:
		subtree->printer = sprint_integer;
		break;
	    case TYPE_NETADDR:
		subtree->printer = sprint_networkaddress;
		break;
	    case TYPE_IPADDR:
		subtree->printer = sprint_ipaddress;
		break;
	    case TYPE_COUNTER:
		subtree->printer = sprint_counter;
		break;
	    case TYPE_GAUGE:
		subtree->printer = sprint_gauge;
		break;
	    case TYPE_TIMETICKS:
		subtree->printer = sprint_timeticks;
		break;
	    case TYPE_OPAQUE:
		subtree->printer = sprint_opaque;
		break;
	    case TYPE_NULL:
		subtree->printer = sprint_null;
		break;
	    case TYPE_BITSTRING:
		subtree->printer = sprint_bitstring;
		break;
	    case TYPE_NSAPADDRESS:
		subtree->printer = sprint_nsapaddress;
		break;
	    case TYPE_COUNTER64:
		subtree->printer = sprint_counter64;
		break;
	    case TYPE_UINTEGER:
		subtree->printer = sprint_uinteger;
		break;
	    case TYPE_UNSIGNED32:
		subtree->printer = sprint_gauge;
		break;
	    case TYPE_OTHER:
	    default:
		subtree->printer = sprint_unknowntype;
		break;
	}
}

/*
 * Read an object identifier from input string into internal OID form.
 * Returns 1 if successful.
 * If an error occurs, this function returns 0 and MAY set snmp_errno.
 * snmp_errno is NOT set if SET_SNMP_ERROR evaluates to nothing.
 * This can make multi-threaded use a tiny bit more robust.
 */
int read_objid(const char *input,
	       oid *output,
	       size_t *out_len)   /* number of subid's in "output" */
{
    struct tree *root = tree_top;
    char buf[SPRINT_MAX_LEN];
    int ret, max_out_len;
    char *name, ch;
    const char *cp;

    cp = input;
    while ((ch = *cp))
	if (('0' <= ch && ch <= '9')
            || ('a' <= ch && ch <= 'z')
	    || ('A' <= ch && ch <= 'Z')
	    || ch == '-')
	    cp++;
	else
	    break;
    if (ch == ':')
	return get_node(input, output, out_len);

    if (*input == '.')
	input++;
    else {
    /* get past leading '.', append '.' to Prefix. */
	if (*Prefix == '.')
	    strcpy(buf, Prefix+1);
	else
            strcpy(buf, Prefix);
	strcat(buf, ".");
	strcat(buf, input);
	input = buf;
    }

    if (root == NULL){
	SET_SNMP_ERROR(SNMPERR_NOMIB);
	*out_len = 0;
	return 0;
    }
    name = strdup(input);
    max_out_len = *out_len;
    *out_len = 0;
    if ((ret = _add_strings_to_oid(root, name, output, out_len, max_out_len)) <= 0)
    {
	if (ret == 0) ret = SNMPERR_UNKNOWN_OBJID;
	SET_SNMP_ERROR(ret);
	free(name);
	return 0;
    }
    free(name);

    return 1;
}


static struct tree *
_sprint_objid(char *buf,
	     oid *objid,
	     size_t objidlen)	/* number of subidentifiers */
{
    char    tempbuf[SPRINT_MAX_LEN], *cp;
    struct tree    *subtree = tree_head;
    char *midpoint = 0;

    *tempbuf = '.';	/* this is a fully qualified name */
    subtree = _get_symbol(objid, objidlen, subtree, tempbuf + 1, 0, &midpoint);
    if (ds_get_boolean(DS_LIBRARY_ID,DS_LIB_PRINT_NUMERIC_OIDS)) {
        cp = tempbuf;
    } else if (ds_get_int(DS_LIBRARY_ID, DS_LIB_PRINT_SUFFIX_ONLY)){
	for(cp = tempbuf; *cp; cp++)
	    ;
        if (midpoint)
            cp = midpoint-2; /* beyond the '.' */
        else {
            while(cp >= tempbuf){
                if (isalpha(*cp))
                    break;
                cp--;
            }
        }
	while(cp >= tempbuf){
	    if (*cp == '.')
		break;
	    cp--;
	}
	cp++;
	if (ds_get_int(DS_LIBRARY_ID, DS_LIB_PRINT_SUFFIX_ONLY) == 2 && cp > tempbuf) {
	    char modbuf[256];
	    char *mod = module_name(subtree->modid, modbuf);
	    size_t len = strlen(mod);
	    if ((int)len+1 >= cp-tempbuf) {
		memmove(tempbuf+len+2, cp, strlen(cp)+1);
		cp = tempbuf+len+2;
	    }
	    cp -= len+2;
	    memcpy(cp, mod, len);
	    cp[len] = ':';
	    cp[len+1] = ':';
	}
    }
    else if (!ds_get_boolean(DS_LIBRARY_ID, DS_LIB_PRINT_FULL_OID)) {
	PrefixListPtr pp = &mib_prefixes[0];
	int ii;
	size_t ilen, tlen;
	const char *testcp;
	cp = tempbuf; tlen = strlen(tempbuf);
	ii = 0;
	while (pp->str) {
	    ilen = pp->len; testcp = pp->str;
	    if ((tlen > ilen) && !memcmp(tempbuf, testcp, ilen)) {
		cp += (ilen + 1);
		break;
	    }
	    pp++;
	}
    }
    else cp = tempbuf;
    strcpy(buf, cp);
    return subtree;
}

char * sprint_objid(char *buf, oid *objid, size_t objidlen)
{
    _sprint_objid(buf,objid,objidlen);
    return buf;
}

void
print_objid(oid *objid,
	    size_t objidlen)	/* number of subidentifiers */
{
  fprint_objid(stdout, objid, objidlen);
}

void
fprint_objid(FILE *f,
	     oid *objid,
	     size_t objidlen)	/* number of subidentifiers */
{
    char    buf[SPRINT_MAX_LEN];

    _sprint_objid(buf, objid, objidlen);
    fprintf(f, "%s\n", buf);
}

void
sprint_variable(char *buf,
		oid *objid,
		size_t objidlen,
		struct variable_list *variable)
{
    struct tree    *subtree;

    subtree = _sprint_objid(buf, objid, objidlen);
    if (!ds_get_boolean(DS_LIBRARY_ID, DS_LIB_PRINT_BARE_VALUE)) {
	buf += strlen(buf);
	if (ds_get_boolean(DS_LIBRARY_ID, DS_LIB_QUICK_PRINT))
	    strcat(buf, " ");
	else
	    strcat(buf, " = ");
	buf += strlen(buf);
    }

    if (variable->type == SNMP_NOSUCHOBJECT)
	strcpy(buf, "No Such Object available on this agent");
    else if (variable->type == SNMP_NOSUCHINSTANCE)
	strcpy(buf, "No Such Instance currently exists");
    else if (variable->type == SNMP_ENDOFMIBVIEW)
	strcpy(buf, "No more variables left in this MIB View");
    else if (subtree) {
	if (subtree->printer)
	    (*subtree->printer)(buf, variable, subtree->enums, subtree->hint, subtree->units);
	  else {
	    sprint_by_type(buf, variable, subtree->enums, subtree->hint, subtree->units);
	  }
    }
    else { /* handle rare case where tree is empty */
        sprint_by_type(buf, variable, 0, 0, 0);
    }
}

void
print_variable(oid *objid,
	       size_t objidlen,
	       struct variable_list *variable)
{
    fprint_variable(stdout, objid, objidlen, variable);
}

void
fprint_variable(FILE *f,
		oid *objid,
		size_t objidlen,
		struct variable_list *variable)
{
    char    buf[SPRINT_MAX_LEN];

    sprint_variable(buf, objid, objidlen, variable);
    fprintf(f, "%s\n", buf);
}

void
sprint_value(char *buf,
	     oid *objid,
	     size_t objidlen,
	     struct variable_list *variable)
{
    char    tempbuf[SPRINT_MAX_LEN];
    struct tree    *subtree = tree_head;

    if (variable->type == SNMP_NOSUCHOBJECT)
	sprintf(buf, "No Such Object available on this agent");
    else if (variable->type == SNMP_NOSUCHINSTANCE)
	sprintf(buf, "No Such Instance currently exists");
    else if (variable->type == SNMP_ENDOFMIBVIEW)
	sprintf(buf, "No more variables left in this MIB View");
    else {
	subtree = get_symbol(objid, objidlen, subtree, tempbuf);
	if (subtree->printer)
	    (*subtree->printer)(buf, variable, subtree->enums, subtree->hint, subtree->units);
	else {
	    sprint_by_type(buf, variable, subtree->enums, subtree->hint, subtree->units);
	}
    }
}

void
print_value(oid *objid,
	    size_t objidlen,
	    struct variable_list *variable)
{
    fprint_value(stdout, objid, objidlen, variable);
}

void
fprint_value(FILE *f,
	     oid *objid,
	     size_t objidlen,
	     struct variable_list *variable)
{
    char    tempbuf[SPRINT_MAX_LEN];

    sprint_value(tempbuf, objid, objidlen, variable);
    fprintf(f, "%s\n", tempbuf);
}


/*
 * Append a quoted printable string to buffer "buf"
 * that represents a range of sub-identifiers "objid".
 *
 * Display '.' for all non-printable sub-identifiers.
 * If successful, "buf" points past the appended string.
 */
char *
dump_oid_to_string(oid *objid,
                   size_t objidlen,
                   char *buf,
                   char quotechar)
{
  if (buf)
  { int ii, alen;
    char *scp;
    char *cp = buf + (strlen(buf));
    scp = cp;
    for (ii= 0, alen = 0; ii < (int)objidlen; ii++)
    {
        oid tst = objid[ii];
        if ((tst > 254) || (!isprint(tst)))
            tst = (oid)'.';

        if (alen == 0) {
	    if (ds_get_boolean(DS_LIBRARY_ID,DS_LIB_ESCAPE_QUOTES))
		*cp++ = '\\';
	    *cp++ = quotechar;
	}
        *cp++ = (char)tst;
        alen++;
    }
    if (alen) {
	if (ds_get_boolean(DS_LIBRARY_ID,DS_LIB_ESCAPE_QUOTES))
	    *cp++ = '\\';
	*cp++ = quotechar;
    }
    *cp = '\0';
    buf = cp;
  }

  return buf;
}

static struct tree *
_get_symbol(oid *objid,
	   size_t objidlen,
	   struct tree *subtree,
	   char *buf,
	   struct index_list *in_dices,
           char **end_of_known)
{
    struct tree    *return_tree = NULL;
    int extended_index = ds_get_boolean(DS_LIBRARY_ID, DS_LIB_EXTENDED_INDEX);

    if (!objid || !buf)
        return NULL;

    for(; subtree; subtree = subtree->next_peer){
	if (*objid == subtree->subid){
	    if (subtree->indexes)
		in_dices = subtree->indexes;
	    else if (subtree->augments) {
	        struct tree *tp2 = find_tree_node(subtree->augments, -1);
		if (tp2) in_dices = tp2->indexes;
	    }
	    if (!strncmp( subtree->label, ANON, ANON_LEN) ||
                ds_get_boolean(DS_LIBRARY_ID, DS_LIB_PRINT_NUMERIC_OIDS))
                sprintf(buf, "%lu", subtree->subid);
	    else
                strcpy(buf, subtree->label);
	    if (objidlen > 1){
		while(*buf)
		    buf++;
		*buf++ = '.';
		*buf = '\0';

		return_tree = _get_symbol(objid + 1, objidlen - 1, subtree->child_list,
					 buf, in_dices, end_of_known);
	    }
	    if (return_tree != NULL)
		return return_tree;
	    else
		return subtree;
	}
    }

    if (end_of_known)
        *end_of_known = buf;

    /* subtree not found */

    while (in_dices && (objidlen > 0) &&
           !ds_get_boolean(DS_LIBRARY_ID, DS_LIB_PRINT_NUMERIC_OIDS) &&
           !ds_get_boolean(DS_LIBRARY_ID, DS_LIB_DONT_BREAKDOWN_OIDS)) {
	size_t numids;
	struct tree *tp;
	tp = find_tree_node(in_dices->ilabel, -1);
	if (!tp) {
            /* ack.  Can't find an index in the mib tree.  bail */
            goto finish_it;
        }
	if (extended_index) {
	    if (buf[-1] == '.') buf--;
	    *buf++ = '[';
	    *buf = 0;
	}
	switch(tp->type) {
	case TYPE_OCTETSTR:
	    if (extended_index && tp->hint) {
	    	struct variable_list var;
		char buffer[1024];
		int i;
		memset(&var, 0, sizeof var);
		if (in_dices->isimplied) {
		    numids = objidlen;
		    if (numids > objidlen)
			goto finish_it;
		} else if (tp->ranges && !tp->ranges->next
		  	 && tp->ranges->low == tp->ranges->high) {
		    numids = tp->ranges->low;
		    if (numids > objidlen)
			goto finish_it;
		} else {
		    numids = *objid;
		    if (numids >= objidlen)
			goto finish_it;
		    objid++;
		    objidlen--;
		}
		if (numids > objidlen)
		    goto finish_it;
		for (i = 0; i < (int)numids; i++) buffer[i] = (u_char)objid[i];
		var.type = ASN_OCTET_STR;
		var.val.string = buffer;
		var.val_len = numids;
		sprint_octet_string(buf, &var, NULL, tp->hint, NULL);
		while (*buf) buf++;
	    } else if (in_dices->isimplied) {
                numids = objidlen;
                if (numids > objidlen)
                    goto finish_it;
                buf = dump_oid_to_string(objid, numids, buf, '\'');
	    } else if (tp->ranges && !tp->ranges->next
	      	       && tp->ranges->low == tp->ranges->high) {
		/* a fixed-length object string */
	        numids = tp->ranges->low;
                if (numids > objidlen)
                    goto finish_it;
		buf = dump_oid_to_string(objid, numids, buf, '\'');
            } else {
                numids = (size_t)*objid+1;
                if (numids > objidlen)
                    goto finish_it;
		if (numids == 1) {
		    if (ds_get_boolean(DS_LIBRARY_ID,DS_LIB_ESCAPE_QUOTES))
			*buf++ = '\\';
		    *buf++ = '"';
		    if (ds_get_boolean(DS_LIBRARY_ID,DS_LIB_ESCAPE_QUOTES))
			*buf++ = '\\';
		    *buf++ = '"';
		}
		else
		    buf = dump_oid_to_string(objid+1, numids-1, buf, '"');
            }
            objid += numids;
            objidlen -= numids;
	    break;
	case TYPE_INTEGER32:
	case TYPE_UINTEGER:
	case TYPE_UNSIGNED32:
	case TYPE_GAUGE:
	case TYPE_INTEGER:
	    if (tp->enums) {
		struct enum_list *ep = tp->enums;
		while (ep && ep->value != (int)(*objid)) ep = ep->next;
		if (ep) sprintf(buf, "%s", ep->label);
		else sprintf(buf, "%lu", *objid);
	    }
	    else sprintf(buf, "%lu", *objid);
	    while(*buf)
		buf++;
	    objid++;
	    objidlen--;
	    break;
	case TYPE_OBJID:
	    if (in_dices->isimplied) {
                numids = objidlen;
            } else {
                numids = (size_t)*objid+1;
            }
	    if ( numids > objidlen)
		goto finish_it;
	    if (extended_index)
	        if (in_dices->isimplied) _sprint_objid(buf, objid, numids);
		else _sprint_objid(buf, objid+1, numids-1);
	    else _get_symbol(objid, numids, NULL, buf, NULL, NULL);
	    objid += (numids);
	    objidlen -= (numids);
            buf += strlen(buf);
	    break;
	case TYPE_IPADDR:
	    if (objidlen < 4)
	        goto finish_it;
	    sprintf(buf, "%lu.%lu.%lu.%lu",
	    	    objid[0], objid[1], objid[2], objid[3]);
	    objid += 4;
	    objidlen -= 4;
	    while (*buf) buf++;
	    break;
	case TYPE_NETADDR:
	    {   oid ntype;
	     	ntype = *objid++; objidlen--;
		sprintf(buf, "%lu.", ntype);
		buf += strlen(buf);
		switch (ntype) {
		case 1:
		    if (objidlen < 4)
			goto finish_it;
		    sprintf(buf, "%lu.%lu.%lu.%lu",
			    objid[0], objid[1], objid[2], objid[3]);
		    objid += 4;
		    objidlen -= 4;
		    break;
		default:
		    goto finish_it;
		}
	    }
	    while (*buf) buf++;
	    break;
	case TYPE_NSAPADDRESS:
	default:
	    goto finish_it;
	    break;
	}
	if (extended_index) *buf++ = ']';
	else *buf++ = '.';
	*buf = 0;
        in_dices = in_dices->next;
    }

finish_it:
    if (buf[-1] != '.') *buf++ = '.';
    while(objidlen-- > 0){	/* output rest of name, uninterpreted */
	sprintf(buf, "%lu.", *objid++);
	while(*buf)
	    buf++;
    }
    buf[-1] = '\0'; /* remove trailing dot */
    return NULL;
}

struct tree *
get_symbol(oid *objid,
	   size_t objidlen,
	   struct tree *subtree,
	   char *buf)
{
   return _get_symbol(objid, objidlen, subtree, buf, NULL, NULL);
}

/*
 * Clone of get_symbol that doesn't take a buffer argument
 */
struct tree *
get_tree(oid *objid,
	 size_t objidlen,
	 struct tree *subtree)
{
    struct tree    *return_tree = NULL;

    for(; subtree; subtree = subtree->next_peer){
        if (*objid == subtree->subid)
            goto found;
    }

    return NULL;

found:
    if (objidlen > 1)
        return_tree = get_tree(objid + 1, objidlen - 1, subtree->child_list);
    if (return_tree != NULL)
        return return_tree;
    else
        return subtree;
}

void
print_description(oid *objid,
		  size_t objidlen,   /* number of subidentifiers */
		  int width)
{
    fprint_description(stdout, objid, objidlen, width);
}

void
fprint_description(FILE *f,
		   oid *objid,
		   size_t objidlen,   /* number of subidentifiers */
		   int width)
{
    struct tree *tp = get_tree(objid, objidlen, tree_head);
    struct tree *subtree = tree_head;
    int pos, len;
    char buf[128];
    const char *cp;

    if (tp->type <= TYPE_SIMPLE_LAST)
        cp = "OBJECT-TYPE";
    else
	switch (tp->type) {
	case TYPE_TRAPTYPE:	cp = "TRAP-TYPE"; break;
	case TYPE_NOTIFTYPE:	cp = "NOTIFICATION-TYPE"; break;
	case TYPE_OBJGROUP:	cp = "OBJECT-GROUP"; break;
	case TYPE_AGENTCAP:	cp = "AGENT-CAPABILITIES"; break;
	case TYPE_MODID:	cp = "MODULE-IDENTITY"; break;
	case TYPE_MODCOMP:	cp = "MODULE-COMPLIANCE"; break;
	default:		sprintf(buf, "type_%d", tp->type); cp = buf;
	}
    fprintf(f, "%s %s\n", tp->label, cp);
    print_tree_node(f, tp, width);
    fprintf(f, "::= {");
    pos = 5;
    while (objidlen > 1) {
	for(; subtree; subtree = subtree->next_peer){
	    if (*objid == subtree->subid){
		if (strncmp( subtree->label, ANON, ANON_LEN))
		    sprintf(buf, " %s(%lu)", subtree->label, subtree->subid);
		else
		    sprintf(buf, " %lu", subtree->subid);
		len = strlen(buf);
		if (pos + len + 2 > width) {
		    fprintf(f, "\n     ");
		    pos = 5;
		}
		fprintf(f, "%s", buf);
		pos += len;
		break;
	    }
	}
	if (subtree == 0) break;
	objid++; objidlen--; subtree = subtree->child_list;
	if (subtree == 0) break;
    }
    fprintf(f, " %lu }\n", *objid);
}

void
print_tree_node(FILE *f,
		struct tree *tp, int width)
{
    const char *cp;
    char str[MAXTOKEN];
    int i, prevmod, pos, len;

    if (tp) {
	module_name(tp->modid, str);
	fprintf(f, "  -- FROM\t%s", str);
	for (i = 1, prevmod = tp->modid; i < tp->number_modules; i++) {
	    if (prevmod != tp->module_list[i]) {
	      module_name(tp->module_list[i], str);
	      fprintf(f, ", %s", str);
	    }
	    prevmod = tp->module_list[i];
	}
	fprintf(f, "\n");
	if (tp->tc_index != -1) {
	    fprintf(f, "  -- TEXTUAL CONVENTION %s\n", get_tc_descriptor(tp->tc_index));
	}
	switch (tp->type) {
	case TYPE_OBJID:	cp = "OBJECT IDENTIFIER"; break;
	case TYPE_OCTETSTR:	cp = "OCTET STRING"; break;
	case TYPE_INTEGER:	cp = "INTEGER"; break;
	case TYPE_NETADDR:	cp = "NetworkAddress"; break;
	case TYPE_IPADDR:	cp = "IpAddress"; break;
	case TYPE_COUNTER:	cp = "Counter32"; break;
	case TYPE_GAUGE:	cp = "Gauge32"; break;
	case TYPE_TIMETICKS:	cp = "TimeTicks"; break;
	case TYPE_OPAQUE:	cp = "Opaque"; break;
	case TYPE_NULL:		cp = "NULL"; break;
	case TYPE_COUNTER64:	cp = "Counter64"; break;
	case TYPE_BITSTRING:	cp = "BITS"; break;
	case TYPE_NSAPADDRESS:	cp = "NsapAddress"; break;
	case TYPE_UINTEGER:	cp = "UInteger32"; break;
	case TYPE_UNSIGNED32:	cp = "Unsigned32"; break;
	case TYPE_INTEGER32:	cp = "Integer32"; break;
	default:		cp = NULL; break;
	}
#if SNMP_TESTING_CODE
	if (!cp && (tp->ranges || tp->enums)) { /* ranges without type ? */
	    sprintf(str,"?0 with %s %s ?",
	    tp->ranges ? "Range" : "",
	    tp->enums ? "Enum" : "");
	    cp = str;
	}
#endif /* SNMP_TESTING_CODE */
	if (cp) fprintf(f, "  SYNTAX\t%s", cp);
	if (tp->ranges) {
	    struct range_list *rp = tp->ranges;
	    int first = 1;
	    fprintf(f, " (");
	    while (rp) {
		if (first) first = 0;
		else fprintf(f, " | ");
		if (rp->low == rp->high) fprintf(f, "%d", rp->low);
		else fprintf(f, "%d..%d", rp->low, rp->high);
		rp = rp->next;
	    }
	    fprintf(f, ") ");
	}
	if (tp->enums) {
	    struct enum_list *ep = tp->enums;
	    int first = 1;
	    fprintf(f," { ");
	    pos = 16+strlen(cp)+2;
	    while (ep) {
		if (first) first = 0;
		else fprintf(f, ", ");
		sprintf(str, "%s(%d)", ep->label, ep->value);
		len = strlen(str);
		if (pos+len+2 > width) {
		    fprintf(f, "\n\t\t  ");
		    pos = 18;
		}
		fprintf(f, "%s", str);
		pos += len+2;
		ep = ep->next;
	    }
	    fprintf(f," } ");
	}
	if (cp) fprintf(f, "\n");
	if (tp->hint) fprintf(f, "  DISPLAY-HINT\t\"%s\"\n", tp->hint);
	if (tp->units) fprintf(f, "  UNITS\t\"%s\"\n", tp->units);
	switch (tp->access) {
	case MIB_ACCESS_READONLY:	cp = "read-only"; break;
	case MIB_ACCESS_READWRITE:	cp = "read-write"; break;
	case MIB_ACCESS_WRITEONLY:	cp = "write-only"; break;
	case MIB_ACCESS_NOACCESS:	cp = "not-accessible"; break;
	case MIB_ACCESS_NOTIFY:		cp = "accessible-for-notify"; break;
	case MIB_ACCESS_CREATE:		cp = "read-create"; break;
	case 0:				cp = NULL; break;
	default:			sprintf(str,"access_%d", tp->access); cp = str;
	}
	if (cp) fprintf(f, "  MAX-ACCESS\t%s\n", cp);
	switch (tp->status) {
	case MIB_STATUS_MANDATORY:	cp = "mandatory"; break;
	case MIB_STATUS_OPTIONAL:	cp = "optional"; break;
	case MIB_STATUS_OBSOLETE:	cp = "obsolete"; break;
	case MIB_STATUS_DEPRECATED:	cp = "deprecated"; break;
	case MIB_STATUS_CURRENT:	cp = "current"; break;
	case 0:				cp = NULL; break;
	default:			sprintf(str,"status_%d", tp->status); cp = str;
	}
#if SNMP_TESTING_CODE
	if (!cp && (tp->indexes)) { /* index without status ? */
	    sprintf(str,"?0 with %s ?",
	    tp->indexes ? "Index" : "");
	    cp = str;
	}
#endif /* SNMP_TESTING_CODE */
	if (cp) fprintf(f, "  STATUS\t%s\n", cp);
	if (tp->augments)
	    fprintf(f, "  AUGMENTS\t{ %s }\n", tp->augments);
	if (tp->indexes) {
            struct index_list *ip = tp->indexes;
            int first=1;
            fprintf(f, "  INDEX\t\t");
            fprintf(f,"{ ");
	    pos = 16+2;
	    while (ip) {
		if (first) first = 0;
		else fprintf(f, ", ");
		sprintf(str, "%s%s", ip->isimplied ? "IMPLIED " : "",
			ip->ilabel);
		len = strlen(str);
		if (pos+len+2 > width) {
		    fprintf(f, "\n\t\t  ");
		    pos = 16+2;
		}
		fprintf(f, "%s", str);
		pos += len+2;
		ip = ip->next;
	    }
	    fprintf(f," }\n");
	}
	if (tp->varbinds) {
            struct varbind_list *vp = tp->varbinds;
            int first=1;
            fprintf(f, "  %s\t", tp->type == TYPE_TRAPTYPE ?
	      			   "VARIABLES" : "OBJECTS");
            fprintf(f,"{ ");
	    pos = 16+2;
	    while (vp) {
		if (first) first = 0;
		else fprintf(f, ", ");
		sprintf(str, "%s", vp->vblabel);
		len = strlen(str);
		if (pos+len+2 > width) {
		    fprintf(f, "\n\t\t  ");
		    pos = 16+2;
		}
		fprintf(f, "%s", str);
		pos += len+2;
		vp = vp->next;
	    }
	    fprintf(f," }\n");
	}
	if (tp->description) fprintf(f, "  DESCRIPTION\t\"%s\"\n", tp->description);
	if (tp->defaultValue) fprintf(f, "  DEFVAL\t{ %s }\n", tp->defaultValue);
    }
    else
        fprintf(f, "No description\n");
}

int
get_module_node(const char *fname,
		const char *module,
		oid *objid,
		size_t *objidlen)
{
    int modid, rc = 0;
    struct tree *tp;
    char *name, *cp;

    if ( !strcmp(module, "ANY") )
        modid = -1;
    else {
	read_module(module);
        modid = which_module( module );
	if (modid == -1) return 0;
    }

		/* Isolate the first component of the name ... */
    name = strdup(fname);
    cp = strchr( name, '.' );
    if ( cp != NULL ) {
	*cp = '\0';
	cp++;
    }
		/* ... and locate it in the tree. */
    tp = find_tree_node(name, modid);
    if (tp){
	size_t maxlen = *objidlen;

		/* Set the first element of the object ID */
	if (node_to_oid(tp, objid, objidlen)) {
	    rc = 1;

		/* If the name requested was more than one element,
		   tag on the rest of the components */
	    if (cp != NULL)
	        rc = _add_strings_to_oid(tp, cp, objid, objidlen, maxlen);
	}
    }

    free(name);
    return (rc);
}


/*
 * Populate object identifier from a node in the MIB hierarchy.
 * Build up the object ID, working backwards,
 * starting from the end of the objid buffer.
 * When the top of the MIB tree is reached, adjust the buffer.
 *
 * The buffer length is set to the number of subidentifiers
 * for the object identifier associated with the MIB node.
 * Returns the number of subidentifiers copied.
 *
 * If 0 is returned, the objid buffer is too small,
 * and the buffer contents are indeterminate.
 * The buffer length can be used to create a larger buffer.
 */
static int
node_to_oid(struct tree *tp, oid *objid, size_t *objidlen)
{
    int numids, lenids;
    oid *op;

    if (!tp || !objid || !objidlen)
        return 0;

    lenids = (int)*objidlen;
    op = objid + lenids;  /* points after the last element */

    for(numids = 0; tp; tp = tp->parent, numids++)
    {
        if (numids >= lenids) continue;
        --op;
        *op = tp->subid;
    }

    *objidlen = (size_t)numids;
    if (numids > lenids) {
        return 0;
    }

    if (numids < lenids)
        memmove(objid, op, numids * sizeof(oid));

    return (numids);
}


static int
_add_strings_to_oid(struct tree *tp, char *cp,
             oid *objid, size_t *objidlen,
             size_t maxlen)
{
    oid subid;
    int len_index = 1000000;
    struct tree *tp2 = NULL;
    struct index_list *in_dices = NULL;
    char *fcp, *ecp, *cp2 = NULL;
    char doingquote;
    int len = -1, pos = -1;

    while (cp && tp && tp->child_list) {
	fcp = cp;
	tp2 = tp->child_list;
	/* Isolate the next entry */
	cp2 = strchr( cp, '.' );
	if (cp2) *cp2++ = '\0';

	/* Search for the appropriate child */
	if ( isdigit( *cp ) ) {
	    subid = strtoul(cp, &ecp, 0);
	    if (*ecp) goto bad_id;
	    while (tp2 && tp2->subid != subid) tp2 = tp2->next_peer;
	}
	else {
	    while (tp2 && strcmp(tp2->label, fcp)) tp2 = tp2->next_peer;
	    if (!tp2) goto bad_id;
	    subid = tp2->subid;
	}
	if (*objidlen >= maxlen) goto bad_id;
	objid[ *objidlen ] = subid;
	(*objidlen)++;

	cp = cp2;
	if (!tp2) break;
	tp = tp2;
    }

    if (tp && !tp->child_list) {
	if ((tp2 = tp->parent)) {
	    if (tp2->indexes) in_dices = tp2->indexes;
	    else if (tp2->augments) {
		tp2 = find_tree_node(tp2->augments, -1);
		if (tp2) in_dices = tp2->indexes;
	    }
	}
	tp = NULL;
    }

    while (cp && in_dices) {
	fcp = cp;

	tp = find_tree_node(in_dices->ilabel, -1);
	if (!tp) break;
	switch (tp->type) {
	case TYPE_INTEGER:
	case TYPE_INTEGER32:
	case TYPE_UINTEGER:
	case TYPE_UNSIGNED32:
	    /* Isolate the next entry */
	    cp2 = strchr( cp, '.' );
	    if (cp2) *cp2++ = '\0';
	    if (isdigit(*cp)) {
	    	subid = strtoul(cp, &ecp, 0);
		if (*ecp) goto bad_id;
	    }
	    else {
		if (tp->enums) {
		    struct enum_list *ep = tp->enums;
		    while (ep && strcmp(ep->label, cp)) ep = ep->next;
		    if (!ep) goto bad_id;
		    subid = ep->value;
		}
		else goto bad_id;
	    }
	    if (tp->ranges) {
		struct range_list *rp = tp->ranges;
		int ok = 0;
		while (!ok && rp)
		    if ((rp->low <= (int)subid) && ((int)subid <= rp->high)) ok = 1;
		    else rp = rp->next;
		if (!ok) goto bad_id;
	    }
	    if (*objidlen >= maxlen) goto bad_id;
	    objid[*objidlen] = subid;
	    (*objidlen)++;
	    break;
	case TYPE_IPADDR:
	    if (*objidlen + 4 > maxlen) goto bad_id;
	    for (subid = 0; cp && subid < 4; subid++) {
		fcp = cp; cp2 = strchr(cp, '.'); if (cp2) *cp2++ = 0;
		objid[*objidlen] = strtoul(cp, &ecp, 0);
		if (*ecp) goto bad_id;
		(*objidlen)++;
		cp = cp2;
	    }
	    break;
        case TYPE_OCTETSTR:
	    if (tp->ranges && !tp->ranges->next
	    	&& tp->ranges->low == tp->ranges->high)
		len = tp->ranges->low;
	    else
		len = -1;
	    pos = 0;
	    if (*cp == '"' || *cp == '\'') {
		doingquote = *cp++;
		/* insert length if requested */
		if (!in_dices->isimplied && len == -1) {
		    if (doingquote == '\'') {
		        snmp_set_detail("'-quote is for fixed length strings");
			return 0;
		    }
		    if (*objidlen >= maxlen) goto bad_id;
		    len_index = *objidlen;
		    (*objidlen)++;
		}
		else if (doingquote == '"') {
		    snmp_set_detail("\"-quote is for variable length strings");
		    return 0;
		}

		while(*cp && *cp != doingquote) {
		    if (*objidlen >= maxlen) goto bad_id;
		    objid[ *objidlen ] = *cp++;
		    (*objidlen)++;
		    pos++;
		}
		if (!*cp) goto bad_id;
		cp2 = cp+1;
		if (!*cp2) cp2 = NULL;
		else if (*cp2 != '.') goto bad_id;
		else cp2++;
		if (len == -1) {
		    struct range_list *rp = tp->ranges;
		    int ok = 0;
		    while (rp && !ok)
		    	if (rp->low <= pos && pos <= rp->high) ok = 1;
			else rp = rp->next;
		    if (!ok) goto bad_id;
		    if (!in_dices->isimplied) objid[len_index] = pos;
		}
		else if (pos != len) goto bad_id;
	    }
	    else {
	    	if (!in_dices->isimplied && len == -1) {
		    fcp = cp; cp2 = strchr(cp, '.'); if (cp2) *cp2++ = 0;
		    len = strtoul(cp, &ecp, 0);
		    if (*ecp) goto bad_id;
		    if (*objidlen + len + 1 >= maxlen) goto bad_id;
		    objid[*objidlen] = len;
		    (*objidlen)++;
		    cp = cp2;
		}
		while (len && cp) {
		    fcp = cp; cp2 = strchr(cp, '.'); if (cp2) *cp2++ = 0;
		    objid[*objidlen] = strtoul(cp, &ecp, 0);
		    if (*ecp) goto bad_id;
		    if (objid[*objidlen] < 0 || objid[*objidlen] > 255)
		        goto bad_id;
		    (*objidlen)++;
		    len--;
		    cp = cp2;
		}
	    }
	    break;
	case TYPE_OBJID:
	    break;
	}
	cp = cp2;
	in_dices = in_dices->next;
    }

    while (cp) {
	fcp = cp;
	switch (*cp) {
	case '0': case '1': case '2': case '3': case '4':
	case '5': case '6': case '7': case '8': case '9':
	    cp2 = strchr(cp, '.');
	    if (cp2) *cp2++ = 0;
	    subid = strtoul(cp, &ecp, 0);
	    if (*ecp) goto bad_id;
	    if (*objidlen >= maxlen) goto bad_id;
	    objid[ *objidlen ] = subid;
	    (*objidlen)++;
	    break;
	case '"': case '\'':
	    doingquote = *cp++;
	    /* insert length if requested */
	    if (doingquote == '"') {
		if (*objidlen >= maxlen) goto bad_id;
		objid[ *objidlen ] = len = strchr(cp, doingquote) - cp;
		(*objidlen)++;
	    }

	    while(*cp && *cp != doingquote) {
		if (*objidlen >= maxlen) goto bad_id;
		objid[ *objidlen ] = *cp++;
		(*objidlen)++;
	    }
	    if (!cp) goto bad_id;
	    cp2 = cp+1;
	    if (!*cp2) cp2 = NULL;
	    else if (*cp2 == '.') cp2++;
	    else goto bad_id;
	    break;
	default:
	    goto bad_id;
	}
	cp = cp2;
    }
    return 1;

bad_id:
    {   char buf[256];
	if (in_dices) sprintf(buf, "Index out of range: %s (%s)",
				fcp, in_dices->ilabel);
	else if (tp) sprintf(buf, "Sub-id not found: %s -> %s", tp->label, fcp);
	else sprintf(buf, "%s", fcp);

	snmp_set_detail(buf);
    }
    return 0;
}


/*
 * see comments on find_best_tree_node for usage after first time.
 */
int
get_wild_node(const char *name,
              oid *objid,
              size_t *objidlen)
{
    struct tree *tp = find_best_tree_node(name, tree_head, NULL);
    if (!tp)
        return 0;
    return get_node(tp->label, objid, objidlen);
}

int
get_node(const char *name,
	 oid *objid,
	 size_t *objidlen)
{
    const char *cp;
    char ch;
    int res;

    cp = name;
    while ((ch = *cp))
	if (('0' <= ch && ch <= '9')
	    || ('a' <= ch && ch <= 'z')
	    || ('A' <= ch && ch <= 'Z')
	    || ch == '-')
	    cp++;
	else
	    break;
    if (ch != ':')
	if (*name == '.')
	    res = get_module_node( name+1, "ANY", objid, objidlen );
	else
	    res = get_module_node( name, "ANY", objid, objidlen );
    else {
	char *module;
		/*
		 *  requested name is of the form
		 *	"module:subidentifier"
		 */
	module = (char *)malloc((size_t)(cp-name+1));
	memcpy(module,name,(size_t)(cp-name));
	module[cp-name] = 0;
	cp++;		/* cp now point to the subidentifier */
	if (*cp == ':') cp++;

			/* 'cp' and 'name' *do* go that way round! */
	res = get_module_node( cp, module, objid, objidlen );
	free(module);
    }
    if (res == 0) {
	SET_SNMP_ERROR(SNMPERR_UNKNOWN_OBJID);
    }

    return res;
}

#ifdef testing

main(int argc, char* argv[])
{
    oid objid[MAX_OID_LEN];
    int objidlen = MAX_OID_LEN;
    int count;
    struct variable_list variable;

    init_mib();
    if (argc < 2)
	print_subtree(stdout, tree_head, 0);
    variable.type = ASN_INTEGER;
    variable.val.integer = 3;
    variable.val_len = 4;
    for (argc--; argc; argc--, argv++) {
	objidlen = MAX_OID_LEN;
	printf("read_objid(%s) = %d\n",
	       argv[1], read_objid(argv[1], objid, &objidlen));
	for(count = 0; count < objidlen; count++)
	    printf("%d.", objid[count]);
	printf("\n");
	print_variable(objid, objidlen, &variable);
    }
}

#endif /* testing */

/* initialize: no peers included in the report. */
void clear_tree_flags(register struct tree *tp)
{
    for (; tp; tp = tp->next_peer)
    {
        tp->reported = 0;
        if (tp->child_list)
            clear_tree_flags(tp->child_list); /*RECURSE*/
    }
}

/*
 * Update: 1998-07-17 <jhy@gsu.edu>
 * Added print_oid_report* functions.
 */
static int print_subtree_oid_report_labeledoid = 0;
static int print_subtree_oid_report_oid = 0;
static int print_subtree_oid_report_symbolic = 0;
static int print_subtree_oid_report_suffix = 0;

/* These methods recurse. */
static void print_parent_labeledoid(FILE *, struct tree *);
static void print_parent_oid(FILE *, struct tree *);
static void print_parent_label(FILE *, struct tree *);
static void print_subtree_oid_report(FILE *, struct tree *, int);


void
print_oid_report (FILE *fp)
{
    struct tree *tp;
    clear_tree_flags(tree_head);
    for (tp = tree_head ; tp ; tp=tp->next_peer)
        print_subtree_oid_report (fp, tp, 0);
}

void
print_oid_report_enable_labeledoid (void)
{
    print_subtree_oid_report_labeledoid = 1;
}

void
print_oid_report_enable_oid (void)
{
    print_subtree_oid_report_oid = 1;
}

void
print_oid_report_enable_suffix (void)
{
    print_subtree_oid_report_suffix = 1;
}

void
print_oid_report_enable_symbolic (void)
{
    print_subtree_oid_report_symbolic = 1;
}

/*
 * helper methods for print_subtree_oid_report()
 * each one traverses back up the node tree
 * until there is no parent.  Then, the label combination
 * is output, such that the parent is displayed first.
 *
 * Warning: these methods are all recursive.
 */

static void
print_parent_labeledoid(FILE *f,
			struct tree *tp)
{
    if(tp)
    {
        if(tp->parent)
        {
            print_parent_labeledoid(f, tp->parent); /*RECURSE*/
        }
        fprintf(f, ".%s(%lu)", tp->label, tp->subid);
    }
}

static void
print_parent_oid(FILE *f,
		 struct tree *tp)
{
    if(tp)
    {
        if(tp->parent)
        {
            print_parent_oid(f, tp->parent); /*RECURSE*/
        }
        fprintf(f, ".%lu", tp->subid);
    }
}

static void
print_parent_label(FILE *f,
		   struct tree *tp)
{
    if(tp)
    {
        if(tp->parent)
        {
            print_parent_label(f, tp->parent); /*RECURSE*/
        }
        fprintf(f, ".%s", tp->label);
    }
}

/*
 * print_subtree_oid_report():
 *
 * This methods generates variations on the original print_subtree() report.
 * Traverse the tree depth first, from least to greatest sub-identifier.
 * Warning: this methods recurses and calls methods that recurse.
 */

static void
print_subtree_oid_report(FILE *f,
                         struct tree *tree,
                         int count)
{
    struct tree *tp;

    count++;

    /* sanity check */
    if(!tree)
    {
        return;
    }

    /*
     * find the not reported peer with the lowest sub-identifier.
     * if no more, break the loop and cleanup.
     * set "reported" flag, and create report for this peer.
     * recurse using the children of this peer, if any.
     */
    while (1)
    {
        register struct tree *ntp;

        tp = 0;
        for (ntp = tree->child_list; ntp; ntp = ntp->next_peer)
        {
            if (ntp->reported) continue;

            if (!tp || (tp->subid > ntp->subid))
                tp = ntp;
        }
        if (!tp) break;

        tp->reported = 1;

        if(print_subtree_oid_report_labeledoid)
        {
            print_parent_labeledoid(f, tp);
            fprintf(f, "\n");
        }
        if(print_subtree_oid_report_oid)
        {
            print_parent_oid(f, tp);
            fprintf(f, "\n");
        }
        if(print_subtree_oid_report_symbolic)
        {
            print_parent_label(f, tp);
            fprintf(f, "\n");
        }
        if(print_subtree_oid_report_suffix)
        {
            int i;
            for(i = 0; i < count; i++)
                fprintf(f, "  ");
            fprintf(f, "%s(%ld) type=%d", tp->label, tp->subid, tp->type);
            if (tp->tc_index != -1) fprintf(f, " tc=%d", tp->tc_index);
            if (tp->hint) fprintf(f, " hint=%s", tp->hint);
            if (tp->units) fprintf(f, " units=%s", tp->units);

            fprintf(f, "\n");
        }
        print_subtree_oid_report(f, tp, count); /*RECURSE*/
    }
}


/*
 * Convert timeticks to hours, minutes, seconds string.
 * CMU compatible does not show centiseconds.
 */
char *uptime_string(u_long timeticks, char *buf)
{
    char tbuf[64];
    char * cp;
    uptimeString(timeticks, tbuf);
    cp = strrchr(tbuf, '.');
#ifdef CMU_COMPATIBLE
	if (cp) *cp = '\0';
#endif
    strcpy(buf, tbuf);
    return buf;
}

oid
*snmp_parse_oid(const char *argv,
		oid *root,
		size_t *rootlen)
{
  size_t savlen = *rootlen;
  if (snmp_get_random_access() || strchr(argv, ':')) {
    if (get_node(argv,root,rootlen)) {
      return root;
    }
  } else if (ds_get_boolean(DS_LIBRARY_ID, DS_LIB_REGEX_ACCESS)) {
    if (get_wild_node(argv,root,rootlen)) {
      return root;
    }
  } else {
    if (read_objid(argv,root,rootlen)) {
      return root;
    }
    *rootlen = savlen;
    if (get_node(argv,root,rootlen)) {
      return root;
    }
    *rootlen = savlen;
    DEBUGMSGTL(("parse_oid","wildly parsing\n"));
    if (get_wild_node(argv,root,rootlen)) {
      return root;
    }
  }
  return NULL;
}

#ifdef CMU_COMPATIBLE

int mib_TxtToOid(char *Buf, oid **OidP, size_t *LenP)
{
    return read_objid(Buf, *OidP, LenP);
}

int mib_OidToTxt(oid *O, size_t OidLen, char *Buf, size_t BufLen)
{
    _sprint_objid(Buf, O, OidLen);
    return 1;
}

#endif /* CMU_COMPATIBLE */
