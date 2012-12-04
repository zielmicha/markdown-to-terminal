/* -*- mode: c, c-basic-offset: 4 -*- */
/* mkd2term.c - man-page-formatted output from markdown text */

/*
 * Copyright (c) 2012, Michał Zieliński <michal@zielinscy.org.pl>
 * Copyright (c) 2009, Baptiste Daroussin and Natacha Porté
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "markdown.h"

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <term.h>

#define READ_UNIT 1024
#define OUTPUT_UNIT 64


/****************************
 * MARKDOWN TO TERMINAL RENDERER *
 ****************************/

#define TERM_COLOR_H1 2
#define TERM_COLOR_H2 3
#define TERM_COLOR_H3 1
#define INDENT "    "
#define NEWLINE_INDENT "\n" INDENT

// not thread safe
struct buf* current_buf;

int term_initialized;

static int buffer_putc(int ch) {
	bufputc(current_buf, ch);
	return 0;
}

static void put_term(struct buf* ob, char* str) {
	current_buf = ob;
	tputs(str, 1, buffer_putc);
}

static void put_term_color(struct buf* ob, int color) {
	put_term(ob, tparm(tigetstr("setf"), color));
}

static void put_term_normal(struct buf* ob) {
	put_term(ob, tigetstr("sgr0"));
}

char translate_entity(char* s) {
	if(strcmp(s, "quot") == 0) {
      return '\'';
    } else if(strcmp(s, "gt") == 0) {
      return '>';
    } else if(s[0] == '#') {
      return (char)atoi(s + 1);
    } else {
		return 0;
	}
}

char get_entity(char* str, int pos, int size) {
	int my_pos = pos;
	my_pos ++;
	for(; my_pos < size && my_pos < pos + 20; my_pos++) {
		if(str[my_pos] == ';') {
			str[my_pos] = '\0';
			char* ent = str + pos + 1;
			return translate_entity(ent);
		}
	}
	return 0;
}

void
term_entity(struct buf *ob, struct buf* text, void* opaque) {
	char ent = get_entity(text->data, 0, text->size);
	if(ent != 0)
		bufputc(ob, ent);
	else
		bufput(ob, text->data, text->size);
}

void
term_text_escape(struct buf *ob, char *src, size_t size) {
	// this should actually escape terminal escape character
	int i = 0;
	while(i < size) {
		char ch = src[i];
		if(ch == '\n') BUFPUTSL(ob, NEWLINE_INDENT);
		else bufputc(ob, ch);
		i++;
	}
}

static void
term_blockcode(struct buf *ob, struct buf *text, void *opaque) {
	BUFPUTSL(ob, "~~~~" NEWLINE_INDENT);
	if (text) term_text_escape(ob, text->data, text->size);
	BUFPUTSL(ob, "~~~~" NEWLINE_INDENT);
}

static int
term_codespan(struct buf *ob, struct buf *text, void *opaque) {
	BUFPUTSL(ob, "~~~~" NEWLINE_INDENT);
	if (text) term_text_escape(ob, text->data, text->size);
	BUFPUTSL(ob, "~~~~" NEWLINE_INDENT);
	return 1;
}

static void
term_header(struct buf *ob, struct buf *text, int level, void *opaque) {
	bufputc(ob, '\n');
	switch(level) {
    case 1:
        put_term_color(ob, TERM_COLOR_H1);
        break;
    case 2:
        put_term_color(ob, TERM_COLOR_H2);
        break;
    case 3:
        BUFPUTSL(ob, "  ");
        put_term_color(ob, TERM_COLOR_H3);
        break;
	}
	put_term(ob, tigetstr("smul"));
	put_term(ob, tigetstr("bold"));
	if (text) bufput(ob, text->data, text->size);
	put_term_normal(ob);
	BUFPUTSL(ob, NEWLINE_INDENT);
}

static int
term_double_emphasis(struct buf *ob, struct buf *text, char c, void *opaque) {
	if (!text || !text->size) return 0;
	put_term(ob, tigetstr("bold"));
	put_term(ob, tigetstr("smul"));
	bufput(ob, text->data, text->size);
	put_term_normal(ob);
	return 1;
}

static int
term_emphasis(struct buf *ob, struct buf *text, char c, void *opaque) {
	if (!text || !text->size) return 0;
	put_term(ob, tigetstr("bold"));
	if (text) bufput(ob, text->data, text->size);
	put_term_normal(ob);
	return 1;
}

static int
term_linebreak(struct buf *ob, void *opaque) {
	BUFPUTSL(ob, NEWLINE_INDENT);
	return 1;
}

static void
term_paragraph(struct buf *ob, struct buf *text, void *opaque) {
	if (ob->size) BUFPUTSL(ob, NEWLINE_INDENT);
	//	BUFPUTSL(ob, "\n");
	if (text) bufput(ob, text->data, text->size);
	BUFPUTSL(ob, NEWLINE_INDENT);
}

static void
term_list(struct buf *ob, struct buf *text, int flags, void *opaque) {
	BUFPUTSL(ob, INDENT);
	if (text) bufput(ob, text->data, text->size);
	BUFPUTSL(ob, NEWLINE_INDENT);
 }

static void
term_listitem(struct buf *ob, struct buf *text, int flags, void *opaque) {

	if (flags & MKD_LIST_ORDERED)
		BUFPUTSL(ob, "# ");
	else
		BUFPUTSL(ob, "* ");

	if (text) {
		while (text->size && text->data[text->size - 1] == '\n')
			text->size -= 1;
		int i;
		for(i=0; i<text->size; i++) {
			char ch = text->data[i];
			if(ch == '\n')
				BUFPUTSL(ob, NEWLINE_INDENT);
			else
				bufputc(ob, text->data[i]);
		}
	}
	BUFPUTSL(ob, "");
}

static void
term_normal_text(struct buf *ob, struct buf *text, void *opaque) {
	if (text) term_text_escape(ob, text->data, text->size);
}


/* renderer structure */
struct mkd_renderer to_man = {
	/* document-level callbacks */
	NULL,
	NULL,

	/* block-level callbacks */
	term_blockcode,
	NULL,
	NULL,
	term_header,
	NULL,
	term_list,
	term_listitem,
	term_paragraph,
	NULL,
	NULL,
	NULL,

	/* span-level callbacks */
	NULL,
	term_codespan,
	term_double_emphasis,
	term_emphasis,
	NULL,
	term_linebreak,
	NULL,
	NULL,
	NULL,

	/* low-level callbacks */
	term_entity,
	term_normal_text,

	/* renderer data */
	64,
	"*_",
	NULL
};

/*****************
 * MAIN FUNCTION *
 *****************/

/* main • main function, interfacing STDIO with the parser */
int
main(int argc, char **argv) {
	setupterm(NULL, 1, NULL);

	struct buf *ib, *ob;
	size_t ret;
	FILE *in = stdin;

	/* opening the file if given from the command line */
	if (argc > 1) {
		in = fopen(argv[1], "r");
		if (!in) {
			fprintf(stderr,"Unable to open input file \"%s\": %s\n",
				argv[1], strerror(errno));
			return 1; } }

	/* reading everything */
	ib = bufnew(READ_UNIT);
	bufgrow(ib, READ_UNIT);
	while ((ret = fread(ib->data + ib->size, 1,
			ib->asize - ib->size, in)) > 0) {
		ib->size += ret;
		bufgrow(ib, ib->size + READ_UNIT); }
	if (in != stdin) fclose(in);

	/* performing markdown to man */
	ob = bufnew(OUTPUT_UNIT);
	markdown(ob, ib, &to_man);

	/* writing the result to stdout */
	ret = fwrite(ob->data, 1, ob->size, stdout);
	if (ret < ob->size)
		fprintf(stderr, "Warning: only %zu output byte written, "
				"out of %zu\n",
				ret,
				ob->size);

	/* cleanup */
	bufrelease(ib);
	bufrelease(ob);
	printf("\n");
	return 0;
}
