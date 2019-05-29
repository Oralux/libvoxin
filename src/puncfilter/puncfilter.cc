/*
 * puncFilter.cc - punctuation filter
 *
 * Copyright (C) 2009, Gilles Casse <gcasse@oralux.org>
 *
 * This is free software; you can redistribute it and/or modify it under the
 * terms of the GNU Lesser General Public License as published by the Free
 * Software Foundation; either version 2.1, or (at your option) any later
 * version.
 *
 * This software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this package; see the file COPYING.  If not, write to the Free
 * Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 */


extern "C" {
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>
#include <iconv.h>
#include <assert.h>
#include "debug.h"
}

#include <list>
#include "puncfilter.h"


#define ICONV_ERROR ((iconv_t)-1)

#define CHAR_LENGTH_UNDEFINED 0
#define CHAR_LENGTH_ONE_BYTE 1
#define CHAR_LENGTH_TWO_BYTES 2
static const int charset_nb_bytes[] = {
	CHAR_LENGTH_UNDEFINED, //CHARSET_UNDEFINED
	CHAR_LENGTH_ONE_BYTE, //CHARSET_ISO_8859_1
	CHAR_LENGTH_ONE_BYTE|CHAR_LENGTH_TWO_BYTES, //CHARSET_GBK
	CHAR_LENGTH_TWO_BYTES, //CHARSET_UCS-2
	CHAR_LENGTH_ONE_BYTE|CHAR_LENGTH_TWO_BYTES, //CHARSET_BIG_5
	CHAR_LENGTH_ONE_BYTE|CHAR_LENGTH_TWO_BYTES, //CHARSET_SJIS
};

static const char* charset_name[] = {
	NULL,
	"ISO-8859-1//IGNORE",
	"GBK//IGNORE",
	"UCS2//IGNORE",
	"BIG5//IGNORE",
	"SJIS//IGNORE",
};

static void convert_xml_predefined_entities(wchar_t *src, int *nb_wchar);

// #define MAX_PUNCT 256
// static wchar_t _iswpunct[MAX_PUNCT];
// #define NB_ISWPUNCT (sizeof(_iswpunct)/sizeof(_iswpunct[0]))

// static int _ibmtts_punct[] = {
//     0x21, /* ! */
//     0x22, /* " */
//     0x27, /* ' */
//     0x28, /* ( */
//     0x29, /* ) */
//     0x2c, /* , */
//     0x2e, /* . */
//     0x3a, /* : */
//     0x3b, /* ; */
//     0x3f, /* ? */
// };
// #define NB_IBMTTS_PUNCT (sizeof(_ibmtts_punct)/sizeof(_ibmtts_punct[0]))


puncFilter::puncFilter()
	: my_filtered_text(NULL),
	  my_mode(PUNC_NONE),
	  my_punctuation(NULL),
	  my_punctuation_num(0)
{
	int i;

	ENTER();

	//     memset(_iswpunct, 0, MAX_PUNCT*sizeof(wchar_t));
	//     for (i=0; i<NB_IBMTTS_PUNCT; i++) {
	//             _iswpunct[_ibmtts_punct[i]] = 1;
	//         }
}

puncFilter::~puncFilter()
{
	ENTER();
	if (my_filtered_text) {
		free(my_filtered_text);
		my_filtered_text = NULL;
	}

	if (my_punctuation) {
		free(my_punctuation);
		my_punctuation = NULL;
	}
	my_punctuation_num = 0;

}

void puncFilter::setMode(const char* var)
{
	int err=0;

	dbg("puncFilter::%s: var=%s", __func__, var);

	if (!var || (*var < '0') || (*var > '2'))
		return;

	mode a_mode = (puncFilter::mode)(*var -'0');
	if (a_mode != PUNC_SOME) {
		my_mode = a_mode;
		return;
	}

	int len = strlen(var);
	if (len < 2)
		return;

	iconv_t a_utf8_to_wchar_convertor = iconv_open("WCHAR_T", "UTF-8");
	if (a_utf8_to_wchar_convertor == ICONV_ERROR) {
		dbg("Error iconv_open: from %s to %s", "UTF-8", "WCHAR_T");
		return;
	}

	char *inbuf = (char*)(var + 1);
	size_t inbytesleft = len - 1;
	size_t outbytesleft = inbytesleft*sizeof(wchar_t);
	wchar_t* a_punctuation = (wchar_t*)calloc(1, outbytesleft + sizeof(L'\0'));
	char* outbuf = (char*)a_punctuation;
	size_t status = iconv(a_utf8_to_wchar_convertor,
						  &inbuf, &inbytesleft,
						  &outbuf, &outbytesleft);

	if (inbytesleft) {
		free(a_punctuation);
		dbg("Failed to convert utf-8 to wchar_t, status=0x%lx, inbytesleft=%ld", (long int)status, (long int)inbytesleft);
	}
	else {
		my_mode = a_mode;

		if (my_punctuation){
			free(my_punctuation);
		}
		my_punctuation = a_punctuation;
		my_punctuation_num = wcslen(a_punctuation);
		dbg("%d punctuation(s)=|%ls|", my_punctuation_num, my_punctuation);
	}

	iconv_close(a_utf8_to_wchar_convertor);
}


bool puncFilter::filterText(const char *msg_in_utf8, charset_t charset, const char **clientFilteredText)
{
	bool a_status = true;
	size_t bytes_in = 0;
	size_t bytes_out = 0;
	list<wchar_t *> a_list;
	int nb_of_punct = 0;
	int w_total_src = 0;
	size_t inbytesleft = 0;
	size_t outbytesleft = 0;
	char *inbuf = NULL;
	char *outbuf = NULL;
	wchar_t *src = NULL;
	wchar_t *dest = NULL;
	size_t status;

	ENTER();

	if (!msg_in_utf8 || !clientFilteredText || !charset || (charset >= CHARSET_MAX)) {
		dbg("FilterInternalError");
		return false;
	}

	*clientFilteredText = NULL;

	if (my_filtered_text) {
		free(my_filtered_text);
		my_filtered_text = NULL;
	}

	if (!strncmp(msg_in_utf8,"`Pf", 3)) {
		dbg("LEAVE (%d)", __LINE__);
		setMode(msg_in_utf8+3);
		my_filtered_text = (char*)malloc(2);
		strcpy(my_filtered_text," ");
		*clientFilteredText = my_filtered_text;
		return true;
	}

	bytes_in = strlen(msg_in_utf8);
	if (!bytes_in) {
		dbg("LEAVE (%d)", __LINE__);
		my_filtered_text = (char*)malloc(2);
		strcpy(my_filtered_text," ");
		*clientFilteredText = my_filtered_text;
		return true;
	}

	my_conv_to_output_charset = iconv_open(charset_name[charset], "WCHAR_T");
	my_conv_to_wchar = iconv_open("WCHAR_T", "UTF-8");
    
	if (my_conv_to_output_charset == ICONV_ERROR) {
		dbg("Error iconv_open: from %s to %s", charset_name[charset], "WCHAR_T");
		a_status = false;
		goto end_punct1;
	}

	if (my_conv_to_wchar == ICONV_ERROR) {
		dbg("Error iconv_open: from %s to %s", "WCHAR_T", "UTF-8");
		a_status = false;
		goto end_punct1;
	}

	bytes_out = (bytes_in + 1)*sizeof(wchar_t); // +1 for terminator
	src = (wchar_t *)calloc(1, bytes_out);
	if (!src) {
		a_status = false;
		goto end_punct1;
	}

	inbuf = (char*)msg_in_utf8;
	inbytesleft = bytes_in;
	outbuf = (char*)src;
	outbytesleft = bytes_out;

	status = iconv(my_conv_to_wchar,
		       &inbuf, &inbytesleft,
		       &outbuf, &outbytesleft);
	
	if (inbytesleft) {
		dbg("Failed to convert utf-8 to wchar_t, status=0x%lx, inbytesleft=%ld",  (long unsigned int)status, (long int)inbytesleft);
		a_status = false;
		goto end_punct1;
	}

	bytes_out -= outbytesleft;

	dbg("msg=%s, bytes_in=%ld, bytes_out=%ld", msg_in_utf8, (long int)bytes_in, (long int)bytes_out);
	
	w_total_src = bytes_out/sizeof(wchar_t);
	convert_xml_predefined_entities(src, &w_total_src);
	bytes_out = w_total_src*sizeof(wchar_t);
	find_punctuation(a_list, src, w_total_src, nb_of_punct);
	if (!nb_of_punct) {
		my_filtered_text = (char*)malloc(bytes_in+1);
		inbuf = (char*)src;
		inbytesleft = bytes_out;
		outbuf = my_filtered_text;
		bytes_out = outbytesleft = bytes_in;
		status = iconv(my_conv_to_output_charset,
					   &inbuf, &inbytesleft,
					   &outbuf, &outbytesleft);
		
		if (inbytesleft) {
			dbg("Failed to convert from wchar_t, status=0x%lx, inbytesleft=%ld", (long int)status, (long int)inbytesleft);
			a_status = false;
		} else {
			my_filtered_text[bytes_out-outbytesleft] = 0;      
			dbg("my_filtered_text=%s", (my_filtered_text) ? my_filtered_text : "NULL");      
		}
		goto end_punct1;
	} 

	{
#define W_ANNOTATION L" `ts2 . `ts0 "
#define ONE_WCHAR L" "
#define BYTES_ANNOTATION_LENGTH (sizeof(W_ANNOTATION)-sizeof(ONE_WCHAR)+sizeof(wchar_t))
#define W_ANNOTATION_LENGTH (BYTES_ANNOTATION_LENGTH/sizeof(wchar_t))
		list<wchar_t *>::iterator a_iterator = a_list.begin();
		wchar_t *psrc = src;
		int len = 0;
		dbg("bytes_out=%lu, nb_of_punct=%d", (long int)bytes_out, nb_of_punct);
		assert(bytes_out >= sizeof(wchar_t)*nb_of_punct);
		int bytes_total_dest = (bytes_out-sizeof(wchar_t)*nb_of_punct) + nb_of_punct*BYTES_ANNOTATION_LENGTH;
		wchar_t *pdest = NULL;

		inbytesleft = bytes_total_dest + sizeof(wchar_t);
		pdest = dest = (wchar_t *)malloc(inbytesleft);

		if (!dest) {
			a_status = false;
			goto end_punct1;
		}

		while(a_iterator != a_list.end()) {
			len = (*a_iterator - psrc);
			wmemcpy(pdest, psrc, len);
			pdest += len;

			wmemcpy(pdest, W_ANNOTATION, W_ANNOTATION_LENGTH);
			pdest[6] = *(*a_iterator);
			pdest += W_ANNOTATION_LENGTH;

			psrc = 1 + *a_iterator;
			a_iterator++;
		};

		len = w_total_src - (psrc - src);
		wmemcpy(pdest, psrc, len);
		pdest += len;
		*pdest=0;


		len = inbytesleft; // max expected length
		my_filtered_text = (char *)malloc(len+1);

		inbuf = (char*)dest;
		outbuf = my_filtered_text;
		outbytesleft = len;
		status = iconv(my_conv_to_output_charset,
					   &inbuf, &inbytesleft,
					   &outbuf, &outbytesleft);

		if (inbytesleft) {
			dbg("Failed to convert utf-8 , status=0x%lx, inbytesleft=%ld", (long int)status, (long int)inbytesleft);
			a_status = false;
			goto end_punct1;
		}

		len -= outbytesleft;
		my_filtered_text[len] = 0;

		dbg("my_filtered_text=%s", (my_filtered_text) ? my_filtered_text : "NULL");
	}

end_punct1:
	if (src) {
		free(src);
	}

	if (dest) {
		free(dest);
	}

	if (my_conv_to_wchar != ICONV_ERROR) {
		iconv_close(my_conv_to_wchar);
	}

	if (my_conv_to_output_charset != ICONV_ERROR) {
		iconv_close(my_conv_to_output_charset);
	}

	if (my_filtered_text && (a_status)) {
		*clientFilteredText = my_filtered_text;
	}

	dbg("LEAVE (%s)", msg_in_utf8);

	return a_status;
}


typedef struct {
	const wchar_t *str;
	wchar_t c;
	int l;
} predef_t;

static const predef_t xml_predefined_entity[] = {
	{L"&quot;", L'"',6},
	{L"&amp;", L'&',5},
	{L"&apos;", L'\'',6},
	{L"&lt;", L'<',4},
	{L"&gt;", L'>',4},
};

#define MAX_ENTITY_NB (sizeof(xml_predefined_entity)/sizeof(xml_predefined_entity[0]))
#define MIN_ENTITY_LENGTH 4

static void convert_xml_predefined_entities(wchar_t *src, int *nb_wchar)
{
	wchar_t* psrc = (wchar_t*)src;
	bool xml_filtered = false;
	int i = 0;
	int w = 0;

	if (!psrc || !nb_wchar || !*nb_wchar)
		return;

	while (i < *nb_wchar) {
		int l = 1;
		if (xml_filtered) {
			xml_filtered = (psrc[i] != L'>');
		} else if (psrc[i] == L'&') {
			int j = 0;
			for (j=0; j < MAX_ENTITY_NB; j++) {
				if (!wcsncmp(psrc+i, xml_predefined_entity[j].str, xml_predefined_entity[j].l)) {
					psrc[w] = xml_predefined_entity[j].c;
					w++;
					l = xml_predefined_entity[j].l;
					break;
				}
			}
		} else if (psrc[i] == L'<') {
			xml_filtered = true;
		} else {
			psrc[w] = psrc[i];
			w++;
		}
		i += l;
	}
	*nb_wchar = w;
}


/* Find each punctuation character in the src buffer and put its address in a singly linked list.
   Returns the number of punctuations found. */
void puncFilter::find_punctuation(list<wchar_t*> &the_list, wchar_t *src, int w_src_len, int &count)
{
	wchar_t* psrc = (wchar_t*)src;

	count = 0;

	dbg("ENTER (%ls)", src);

	if (!psrc || !w_src_len)
		return;

	switch (my_mode) {
	case PUNC_ALL:
		while (w_src_len--) {
			//             if (iswpunct(*psrc) && (*psrc != L'`') && (_iswpunct[*psrc % MAX_PUNCT])) {
			if (iswpunct(*psrc) && (*psrc != L'`')) {
				count++;
				the_list.insert(the_list.end (), psrc);
				dbg("punc=|%lc|", *psrc);
			}
			psrc++;
		}
		break;

	case PUNC_SOME:
		while (w_src_len--) {
			int j=0;
			for (j=0; j<my_punctuation_num; j++) {
				if (*psrc == my_punctuation[j]) {
					count++;
					the_list.insert(the_list.end (), psrc);
					dbg("punc=|%lc|", *psrc);
					break;
				}
			}
			psrc++;
		}
		break;

	default:
		break;
	}
}


extern "C" {
	void *puncfilter_create()
	{
		ENTER();
		return (new puncFilter());
	}

	void puncfilter_delete(void *handle)
	{
		ENTER();
		if (handle)
			delete (puncFilter *)handle;
	}

	bool puncfilter_do(void *handle, const char *input, enum charset_t charset, const char **filteredText)
	{
		bool res = false;
		ENTER();
		if (handle && input && filteredText)
			res = ((puncFilter *)handle)->filterText(input, charset, filteredText);
		return res;
	}
}

